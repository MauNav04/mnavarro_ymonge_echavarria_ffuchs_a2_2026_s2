/**
 * ============================================================================
 * Modelo: Reacción-Difusión Gray-Scott (Esquema FTCS - Stencil de 5 puntos)
 * ============================================================================
 */

#include "gray_scott_core.hpp"

#include <algorithm>
#include <fstream>
#include <string>

void init_simulation(Grid2D& grid) {
    // Estado base: Todo el espacio saturado de nutriente U=1.0, sin bacterias V=0.0
    std::fill(grid.u.begin(), grid.u.end(), 1.0f);
    std::fill(grid.v.begin(), grid.v.end(), 0.0f);

    // Inoculación central: Cuadrado de 20x20 en el centro con presencia de bacterias
    int start_x = grid.width / 2 - 10;
    int end_x   = grid.width / 2 + 10;
    int start_y = grid.height / 2 - 10;
    int end_y   = grid.height / 2 + 10;

    for (int y = start_y; y < end_y; ++y) {
        for (int x = start_x; x < end_x; ++x) {
            size_t idx = grid.index(x, y);
            grid.u[idx] = 0.5f;
            grid.v[idx] = 1.0f;
        }
    }
}

// --- VARIABLES CRÍTICAS PARA PARALELIZACIÓN  ---
// [Variable 1]: 'width' y 'height' -> Espacio total de tareas discretas (Grilla N x M)
// [Variable 2]: 'total_steps'      -> Cadena de dependencias temporales estrictas (t -> t+1)
// [Variable 3]: 'Du', 'Dv', 'dt'   -> Coeficientes de difusión y paso temporal (Estabilidad num.)

void step_simulation_sequential(const Grid2D& current, Grid2D& next, const GrayScottParams& params) {
    // --- VARIABLES CLAVE DE DOMINIO (Avance 2) ---
    // [Variable 1]: Espacio bidimensional continuo (w x h celdas independientes)
    int w = current.width;
    int h = current.height;

    // =========================================================================
    // [PUNTO 1 DE PARALELIZACIÓN - Descomposición 1D / CMP / SMT / Coarse-Grained]
    // Bucle exterior por filas (y):
    // - Cada hilo toma un bloque contiguo de filas: [start_y, end_y).
    // - Máxima localidad espacial en Caché L1/L2 al iterar en orden Row-Major.
    // - Mínimo falso compartimiento (False Sharing) entre líneas de caché.
    // =========================================================================
    for (int y = 1; y < h - 1; ++y) {

        // =====================================================================
        // [PUNTO 2 DE PARALELIZACIÓN - Descomposición 2D / Mosaicos / Fine-Grained]
        // Bucle interior por columnas (x):
        // - Permite dividir la matriz en bloques 2D (ej. baldosas de 16x16 o 32x32).
        // - Base para esquemas de grano fino o colas dinámicas de trabajo.
        // =====================================================================
        for (int x = 1; x < w - 1; ++x) {
            size_t idx = current.index(x, y);

            // =================================================================
            // [LECTURA COMPARTIDA - LIBRE DE CONDICIONES DE CARRERA]
            // Todos los hilos leen concurrentemente del búfer 'current'.
            // Al ser de solo lectura durante el paso (t), no requiere cerrojos (locks).
            // =================================================================
            float u_center = current.u[idx];
            float v_center = current.v[idx];

            // Cálculo del Laplaciano mediante Stencil de 5 puntos (vecinos inmediatos)
            float lap_u = current.u[current.index(x + 1, y)] +
                          current.u[current.index(x - 1, y)] +
                          current.u[current.index(x, y + 1)] +
                          current.u[current.index(x, y - 1)] -
                          4.0f * u_center;

            float lap_v = current.v[current.index(x + 1, y)] +
                          current.v[current.index(x - 1, y)] +
                          current.v[current.index(x, y + 1)] +
                          current.v[current.index(x, y - 1)] -
                          4.0f * v_center;

            // Reacción química autocatalítica local: U + 2V -> 3V
            float reaction = u_center * v_center * v_center;

            // Tasas de cambio temporal
            float du = params.Du * lap_u - reaction + params.F * (1.0f - u_center);
            float dv = params.Dv * lap_v + reaction - (params.F + params.K) * v_center;

            // =================================================================
            // [RESOLUCIÓN DE RACE CONDITIONS: DOUBLE BUFFERING EN RAM]
            // Cada celda escribe de manera exclusiva en su índice correspondiente de 'next'.
            // Ningún hilo lee de 'next' mientras se calcula el paso actual.
            // =================================================================
            next.u[idx] = u_center + du * params.dt;
            next.v[idx] = v_center + dv * params.dt;
        }
    }
}

//Logica para renderizar la bacteria V en una imagen PPM ultraligera, donde la intensidad del verde representa la concentración de V.
void export_frame_ppm(const Grid2D& grid, int frame_id, const std::string& output_dir) {
    std::string path = output_dir + "/frame_" + std::to_string(frame_id) + ".ppm";
    std::ofstream out(path);
    if (!out.is_open()) return;

    out << "P3\n" << grid.width << " " << grid.height << "\n255\n";

    for (int i = 0; i < grid.width * grid.height; ++i) {
        int intensity = static_cast<int>(std::clamp(grid.v[i], 0.0f, 1.0f) * 255.0f);
        out << 0 << " " << intensity << " " << 0 << "\n";
    }
}
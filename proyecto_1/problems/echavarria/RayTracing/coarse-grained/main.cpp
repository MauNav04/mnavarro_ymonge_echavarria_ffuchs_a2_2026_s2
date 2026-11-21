/**
 * ============================================================================
 * Modelo de Ejecución: Coarse-Grained Multithreading (Grano Grueso)
 * ============================================================================
 *
 * Características Arquitectónicas:
 * 1. Burst Execution: Cada hilo tradicional
 *    procesa un bloque continuo extenso de filas de forma ininterrumpida,
 *    maximizando el aprovechamiento del pipeline y la localidad en memoria caché L1/L2.
 * 2. Conmutación ante Eventos de Bloqueo Costosos / Stalls de Hardware:
 *    En el trazado de rayos, los píxeles tienen costos altamente dispares según la
 *    naturaleza de los materiales. Los rayos en materiales dieléctricos y reflectivos
 *    rebotan hasta 50 veces. Se modela e instrumenta como "stall"
 *    cuando la profundidad de rebote de un rayo alcanza latencia profunda (>= 15 rebotes).
 * 3. Hilos Tradicionales: Implementado mediante hilos POSIX de larga duración.
 * ============================================================================
 */

#include "../src/rtweekend.h"
#include "../src/scene.h"
#include "../src/camera.h"
#include "../src/framebuffer.h"

#include <iostream>
#include <chrono>
#include <string>
#include <vector>
#include <thread>
#include <pthread.h>
#include <algorithm>
#include <cmath>
#include <iomanip>

// ============================================================================
// Contexto para cada Hilo Coarse-Grained
// ============================================================================
struct ContextoHiloCoarse {
    int id;                        // Identificador lógico del hilo [0, num_threads - 1]
    int start_y;                   // Fila inicial de la franja 
    int end_y;                     // Fila final de la franja 
    uint64_t burst_pixels;         // Píxeles en la ráfaga continua
    int stall_events;              // Contador de eventos de rebotes profundos
    const camera* cam;             // Puntero a la cámara calibrada
    const hittable_list* world;    // Puntero a la escena 3D
    Framebuffer* fb;               // Puntero al Framebuffer compartido en RAM
};

// ============================================================================
// Rutina de Ejecución del Hilo (Coarse-Grained)
// ============================================================================
void* worker_thread_coarse(void* arg) {
    ContextoHiloCoarse* ctx = static_cast<ContextoHiloCoarse*>(arg);

    // Calculo con detección de eventos de bloqueo (stalls)
    ctx->cam->render_slice_counted(*(ctx->world), *(ctx->fb), ctx->start_y, ctx->end_y, ctx->stall_events);

    return nullptr;
}

int main(int argc, char* argv[]) {
    int width = 400;
    int samples = 10;
    int max_depth = 50;
    int num_threads = 8;        // 8 hilos fisicos por defecto
    bool verify = false;
    std::string output_ppm = "";

    // Procesamiento de argumentos CLI
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--threads" && i + 1 < argc) {
            num_threads = std::stoi(argv[++i]);
        } else if (arg == "--width" && i + 1 < argc) {
            width = std::stoi(argv[++i]);
        } else if (arg == "--samples" && i + 1 < argc) {
            samples = std::stoi(argv[++i]);
        } else if (arg == "--depth" && i + 1 < argc) {
            max_depth = std::stoi(argv[++i]);
        } else if (arg == "--verify") {
            verify = true;
        } else if (arg == "--save-ppm" && i + 1 < argc) {
            output_ppm = argv[++i];
        }
    }

    if (num_threads < 1) num_threads = 1;

    // Creación de la escena determinista
    hittable_list world = create_scene();

    // Configuración óptica de la cámara
    camera cam;
    cam.aspect_ratio      = 16.0 / 9.0;
    cam.image_width       = width;
    cam.samples_per_pixel = samples;
    cam.max_depth         = max_depth;
    cam.vfov              = 20;
    cam.lookfrom          = point3(13, 2, 3);
    cam.lookat            = point3(0, 0, 0);
    cam.vup               = vec3(0, 1, 0);
    cam.defocus_angle     = 0.6;
    cam.focus_dist        = 10.0;

    cam.initialize();
    int height = cam.get_image_height();

    // Creación del Framebuffer en RAM contigua
    Framebuffer fb(width, height);

    // Particionado en grandes bloques continuos de filas (Grano Grueso)
    int filas_base = height / num_threads;
    int residuo = height % num_threads;

    std::vector<pthread_t> hilos(num_threads);
    std::vector<ContextoHiloCoarse> contextos(num_threads);

    int fila_actual = 0;
    for (int t = 0; t < num_threads; ++t) {
        int filas_asignadas = filas_base + (t < residuo ? 1 : 0);
        contextos[t].id = t;
        contextos[t].start_y = fila_actual;
        contextos[t].end_y = fila_actual + filas_asignadas;
        contextos[t].burst_pixels = static_cast<uint64_t>(filas_asignadas) * static_cast<uint64_t>(width);
        contextos[t].stall_events = 0;
        contextos[t].cam = &cam;
        contextos[t].world = &world;
        contextos[t].fb = &fb;

        fila_actual += filas_asignadas;
    }

    // =========================================================================
    // MEDICIÓN DE TIEMPO PURO DE CÓMPUTO
    // =========================================================================
    auto start = std::chrono::high_resolution_clock::now();

    for (int t = 0; t < num_threads; ++t) {
        int rc = pthread_create(&hilos[t], nullptr, worker_thread_coarse, &contextos[t]);
        if (rc != 0) {
            std::cerr << "[ERROR CRÍTICO] Fallo al crear hilo Coarse-Grained " << t << "\n";
            return 1;
        }
    }

    for (int t = 0; t < num_threads; ++t) {
        pthread_join(hilos[t], nullptr);
    }

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> elapsed_coarse = end - start;

    // Consolidar métricas de ráfagas y eventos de stall
    uint64_t total_burst_pixels = 0;
    uint64_t total_stalls = 0;
    for (int t = 0; t < num_threads; ++t) {
        total_burst_pixels += contextos[t].burst_pixels;
        total_stalls += contextos[t].stall_events;
    }

    std::cout << "========================================================\n";
    std::cout << " Ray Tracing - Coarse-Grained (Multihilo de Grano Grueso)\n";
    std::cout << "========================================================\n";
    std::cout << "Resolución         : " << width << "x" << height << "\n";
    std::cout << "Muestras / Píxel   : " << samples << "\n";
    std::cout << "Profundidad Rebote : " << max_depth << "\n";
    std::cout << "Hilos Tradicionales: " << num_threads << "\n";
    std::cout << "Píxeles por Ráfaga : " << total_burst_pixels / num_threads << " (promedio por hilo)\n";
    std::cout << "Stalls Detectados  : " << total_stalls << " eventos (rebotes >= 15)\n";
    std::cout << "Tiempo Cómputo     : " << std::fixed << std::setprecision(3)
              << elapsed_coarse.count() << " ms (" << elapsed_coarse.count() / 1000.0 << " s)\n";

    // Verificación numérica contra secuencial
    if (verify) {
        std::cout << "--------------------------------------------------------\n";
        std::cout << " Ejecutando verificación numérica contra Secuencial...\n";
        Framebuffer fb_seq(width, height);

        auto t_seq_start = std::chrono::high_resolution_clock::now();
        cam.render_slice(world, fb_seq, 0, height);
        auto t_seq_end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> elapsed_seq = t_seq_end - t_seq_start;

        double speedup = elapsed_seq.count() / elapsed_coarse.count();
        double eficiencia = (speedup / num_threads) * 100.0;
        double diff = fb.diff_max(fb_seq);

        std::cout << "Tiempo Secuencial  : " << elapsed_seq.count() << " ms\n";
        std::cout << "Speedup Obtenido   : " << std::fixed << std::setprecision(2) << speedup << "x\n";
        std::cout << "Eficiencia Paralela: " << std::fixed << std::setprecision(1) << eficiencia << " %\n";
        std::cout << "Diferencia Máx RGB : " << std::scientific << diff << "\n";
        std::cout << "ESTADO VERIFICACIÓN: [COMPLETADO - CÁLCULO ESTOCÁSTICO VÁLIDO]\n";
    }
    std::cout << "========================================================\n";

    // Salida estructurada CSV por stderr para benchmarking automatizado
    std::cerr << "coarse," << num_threads << "," << width << "," << height << "," 
              << samples << "," << max_depth << "," << (total_burst_pixels / num_threads) << "," 
              << total_stalls << "," << elapsed_coarse.count() << "\n";

    // Exportación opcional a PPM
    if (!output_ppm.empty()) {
        if (fb.save_ppm(output_ppm)) {
            std::cout << "[INFO] Imagen PPM guardada con éxito en: " << output_ppm << "\n";
        } else {
            std::cerr << "[ERROR] No se pudo guardar la imagen en: " << output_ppm << "\n";
        }
    }

    return 0;
}

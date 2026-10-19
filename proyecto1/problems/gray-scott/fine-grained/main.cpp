/**
 * ============================================================================
 * Modelo de Ejecución: Fine-Grained Multithreading (Multihilo de Grano Fino)
 * Problema: Simulación de Reacción-Difusión de Gray-Scott (Proliferación Bacteriana)
 * ============================================================================
 *
 * Fundamentos Microarquitectónicos (CE 4302 - Arquitectura de Computadores II):
 * 1. Planificación Cooperativa por Cuantums Mínimos (Round-Robin Interleaving):
 *    A diferencia de Coarse-Grained (donde el hilo se ejecuta en ráfagas largas y solo
 *    conmuta ante bloqueos costosos como barreras o fallos de caché), en Fine-Grained
 *    la conmutación de hilo ocurre DE FORMA INCONDICIONAL en cada ciclo de reloj
 *    o cuantum mínimo (Q = 1 fila por defecto), independientemente de si el hilo
 *    actual sufrió un stall o no.
 *
 * 2. Propuestas de Implementación :
 *    - Propuesta 1 (Por defecto: --mode cooperative):
 *      Simulación rigurosa del modelo de grano fino mediante planificación cooperativa
 *      en espacio de usuario. Modela un procesador multihilo intercalado donde T hilos 
 lógicos/virtuales rotan por turno estricto
 *      ejecutando un cuantum mínimo (Q = 1 fila) de cálculo. En cada cuantum se registra
 *      una conmutación forzada de contexto (generando millones de eventos cuantificables).
 *    - Propuesta 2 (--mode parallel):
 *      Implementación paralela intercalada mediante hilos del SO (POSIX threads) con
 *      descomposición cíclica (Cyclic Distribution). Cada hilo procesa filas intercaladas
 *      en bloques mínimos (Q filas), evidenciando el impacto de la pérdida de localidad
 *      espacial y la competencia por líneas de caché adyacentes frente a Coarse-Grained.
 *
 * 3. Exactitud Numérica y Descomposición 1D:
 *    Utiliza la función unificada step_simulation_slice(...) del motor numérico común,
 *    garantizando equivalencia matemática exacta celda a celda contra el secuencial.
 * ============================================================================
 */

#include "../include/gray_scott_core.hpp"

#include <iostream>
#include <chrono>
#include <string>
#include <vector>
#include <thread>
#include <pthread.h>
#include <sched.h>
#include <fstream>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <filesystem>
#include <unistd.h>

namespace fs = std::filesystem;

// ============================================================================
// Estructura de Estadísticas por Hilo Virtual / Lógico
// ============================================================================
struct EstadisticasHiloVirtual {
    int id;                         // ID del hilo virtual [0, num_hilos - 1]
    uint64_t cuantums_ejecutados;   // Cantidad de ráfagas mínimas ejecutadas
    uint64_t celdas_computadas;     // Celdas procesadas por el hilo
    uint64_t conmutaciones;         // Conmutaciones forzadas de contexto registradas
};

// ============================================================================
// Estructura de Contexto para el Modo Paralelo Cíclico (POSIX Threads)
// ============================================================================
struct ContextoHiloFineParallel {
    int id;                         // ID lógico del hilo [0, num_threads - 1]
    int num_threads;                // Total de hilos paralelos
    int width;                      // Ancho de la grilla
    int height;                     // Alto de la grilla
    int quantum;                    // Cuantum mínimo (número de filas)
    int total_steps;                // Cantidad total de pasos de simulación
    uint64_t context_switches;      // Conmutaciones registradas por el hilo
    uint64_t computed_cells;        // Celdas computadas
    const GrayScottParams* params;  // Parámetros cinéticos del modelo
    Grid2D* grid_A;                 // Búfer A
    Grid2D* grid_B;                 // Búfer B
    pthread_barrier_t* barrera;     // Barrera inter-paso
    bool save_frames;               // Exportación de fotogramas PPM
    int frame_interval;             // Intervalo de pasos para fotogramas
    std::string output_dir;         // Directorio de salida
    int* frame_counter;             // Puntero a contador de fotogramas
    pthread_barrier_t* barrera_io;  // Barrera para sincronizar operaciones de E/S
};

// ============================================================================
// Rutina de Ejecución: Modo Paralelo Cíclico (POSIX Threads)
// ============================================================================
void* worker_thread_fine_parallel(void* arg) {
    ContextoHiloFineParallel* ctx = static_cast<ContextoHiloFineParallel*>(arg);

    for (int step = 1; step <= ctx->total_steps; ++step) {
        const Grid2D& cur = (step % 2 != 0) ? *(ctx->grid_A) : *(ctx->grid_B);
        Grid2D& nxt       = (step % 2 != 0) ? *(ctx->grid_B) : *(ctx->grid_A);

        // Descomposición Cíclica Intercalada (Fine-Grained):
        // Se divide el dominio en trozos de tamaño `quantum` (por defecto 1 fila).
        // Cada trozo se asigna al hilo (chunk_id % num_threads == id).
        int y = 1;
        while (y < ctx->height - 1) {
            int chunk_id = (y - 1) / ctx->quantum;
            int chunk_size = std::min(ctx->quantum, (ctx->height - 1) - y);

            if (chunk_id % ctx->num_threads == ctx->id) {
                // Ejecución del cuantum mínimo asignado
                step_simulation_slice(cur, nxt, y, y + chunk_size, *(ctx->params));
                ctx->computed_cells += chunk_size * (ctx->width - 2);

                // En grano fino la conmutación se fuerza en cada cuantum
                ctx->context_switches++;
            }
            y += chunk_size;
        }

        // Sincronización al completar todas las filas del paso
        pthread_barrier_wait(ctx->barrera);

        // Exportación opcional de video (E/S en disco)
        if (ctx->save_frames && (step % ctx->frame_interval == 0)) {
            if (ctx->id == 0) {
                export_frame_ppm(nxt, (*ctx->frame_counter)++, ctx->output_dir);
            }
            pthread_barrier_wait(ctx->barrera_io);
        }
    }

    return nullptr;
}

// ============================================================================
// Rutina de Ejecución: Modo Cooperativo Round-Robin (Simulación Pura de Grano Fino)
// ============================================================================
void ejecutar_fine_grained_cooperativo(Grid2D& grid_A, Grid2D& grid_B,
                                      int width, int height,
                                      int total_steps, int num_threads,
                                      int quantum, const GrayScottParams& params,
                                      std::vector<EstadisticasHiloVirtual>& stats,
                                      uint64_t& total_context_switches,
                                      bool save_frames, int frame_interval,
                                      const std::string& output_dir, int& frame_count) {
    stats.resize(num_threads);
    for (int t = 0; t < num_threads; ++t) {
        stats[t] = {t, 0, 0, 0};
    }
    total_context_switches = 0;

    int turno_hilo = 0;

    for (int step = 1; step <= total_steps; ++step) {
        const Grid2D& cur = (step % 2 != 0) ? grid_A : grid_B;
        Grid2D& nxt       = (step % 2 != 0) ? grid_B : grid_A;

        // Planificación en anillo (Round-Robin) por Cuantums Mínimos:
        // El dominio se recorre en fragmentos de tamaño `quantum` (ej: 1 fila).
        // Tras procesar CADA fragmento, la CPU conmuta INCONDICIONALMENTE al siguiente hilo.
        int y = 1;
        while (y < height - 1) {
            int chunk_size = std::min(quantum, (height - 1) - y);

            // 1. El hilo virtual activo ejecuta su cuantum de trabajo
            step_simulation_slice(cur, nxt, y, y + chunk_size, params);

            // Registro de métricas del hilo actual
            stats[turno_hilo].cuantums_ejecutados++;
            stats[turno_hilo].celdas_computadas += chunk_size * (width - 2);
            stats[turno_hilo].conmutaciones++;
            total_context_switches++;

            // 2. CONMUTACIÓN INCONDICIONAL DE CONTEXTO (Round-Robin):
            // Ocurre siempre tras cada cuantum, sin importar si hubo bloqueo o stall
            turno_hilo = (turno_hilo + 1) % num_threads;

            y += chunk_size;
        }

        // Exportación de fotograma PPM si corresponde
        if (save_frames && (step % frame_interval == 0)) {
            export_frame_ppm(nxt, frame_count++, output_dir);
        }
    }
}

// ============================================================================
// Función de Entrada Principal
// ============================================================================
int main(int argc, char* argv[]) {
    int width = 512;
    int height = 512;
    int total_steps = 16000;
    int num_threads = 8;
    int quantum = 1; // Cuantum mínimo por defecto: 1 fila
    std::string mode = "cooperative"; // "cooperative" (defecto) o "parallel"
    bool verify = false;
    bool save_frames = false;
    int frame_interval = 100;
    std::string output_dir = "../videos/temp_frames_fine";

    // Procesamiento de argumentos por línea de comandos
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--size" && i + 1 < argc) {
            width = std::stoi(argv[++i]);
            height = width;
        } else if (arg == "--steps" && i + 1 < argc) {
            total_steps = std::stoi(argv[++i]);
        } else if (arg == "--threads" && i + 1 < argc) {
            num_threads = std::max(1, std::stoi(argv[++i]));
        } else if (arg == "--quantum" && i + 1 < argc) {
            quantum = std::max(1, std::stoi(argv[++i]));
        } else if (arg == "--mode" && i + 1 < argc) {
            mode = argv[++i];
        } else if (arg == "--verify") {
            verify = true;
        } else if (arg == "--save-frames") {
            save_frames = true;
        } else if (arg == "--frame-interval" && i + 1 < argc) {
            frame_interval = std::max(1, std::stoi(argv[++i]));
        } else if (arg == "--output-dir" && i + 1 < argc) {
            output_dir = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Uso: " << argv[0] << " [opciones]\n"
                      << "  --size <N>            Dimensión NxN (por defecto: 512)\n"
                      << "  --steps <S>           Pasos temporales (por defecto: 16000)\n"
                      << "  --threads <T>         Cantidad de hilos (por defecto: 8)\n"
                      << "  --quantum <Q>         Cuantum mínimo en filas (por defecto: 1 fila)\n"
                      << "  --mode <coop|parallel> Modo: 'cooperative' o 'parallel' (defecto: cooperative)\n"
                      << "  --verify              Verificación numérica contra secuencial\n"
                      << "  --save-frames         Guardar fotogramas PPM para video\n"
                      << "  --frame-interval <I>  Intervalo de pasos entre fotogramas (por defecto: 100)\n"
                      << "  --output-dir <DIR>    Directorio para fotogramas PPM\n";
            return 0;
        }
    }

    if (save_frames) {
        fs::create_directories(output_dir);
    }

    // Inicialización de la cinemática de Gray-Scott
    GrayScottParams params;

    // Asignación de doble búfer continuo en memoria contigua
    Grid2D grid_A(width, height);
    Grid2D grid_B(width, height);
    init_simulation(grid_A);
    grid_B = grid_A;

    if (save_frames) {
        export_frame_ppm(grid_A, 0, output_dir);
    }

    int frame_count = 1;
    uint64_t total_context_switches = 0;
    std::vector<EstadisticasHiloVirtual> stats_coop;
    std::chrono::duration<double, std::milli> elapsed_fine;

    // =========================================================================
    // FASE 1: EJECUCIÓN DEL MODELO FINE-GRAINED SEGÚN EL MODO ESCOGIDO
    // =========================================================================
    if (mode == "parallel") {
        pthread_barrier_t barrera;
        pthread_barrier_init(&barrera, nullptr, num_threads);

        pthread_barrier_t barrera_io;
        if (save_frames) {
            pthread_barrier_init(&barrera_io, nullptr, num_threads);
        }

        std::vector<pthread_t> hilos(num_threads);
        std::vector<ContextoHiloFineParallel> contextos(num_threads);

        auto start = std::chrono::high_resolution_clock::now();

        for (int t = 0; t < num_threads; ++t) {
            contextos[t] = {
                t, num_threads, width, height, quantum, total_steps,
                0, 0, &params, &grid_A, &grid_B,
                &barrera, save_frames, frame_interval, output_dir,
                &frame_count, &barrera_io
            };
            pthread_create(&hilos[t], nullptr, worker_thread_fine_parallel, &contextos[t]);
        }

        for (int t = 0; t < num_threads; ++t) {
            pthread_join(hilos[t], nullptr);
            total_context_switches += contextos[t].context_switches;
        }

        auto end = std::chrono::high_resolution_clock::now();
        elapsed_fine = end - start;

        pthread_barrier_destroy(&barrera);
        if (save_frames) {
            pthread_barrier_destroy(&barrera_io);
        }
    } else {
        // Modo cooperativo (Por defecto): Simulación pura de planificación por cuantums
        auto start = std::chrono::high_resolution_clock::now();

        ejecutar_fine_grained_cooperativo(
            grid_A, grid_B, width, height, total_steps,
            num_threads, quantum, params, stats_coop,
            total_context_switches, save_frames, frame_interval,
            output_dir, frame_count
        );

        auto end = std::chrono::high_resolution_clock::now();
        elapsed_fine = end - start;
    }

    const Grid2D& grilla_final = (total_steps % 2 != 0) ? grid_B : grid_A;

    if (!save_frames) {
        std::cout << "========================================================\n";
        std::cout << "  Gray-Scott Fine-Grained (Multihilo de Grano Fino)    \n";
        std::cout << "========================================================\n";
        std::cout << "Dimensiones        : " << width << "x" << height << "\n";
        std::cout << "Pasos Temporales   : " << total_steps << "\n";
        std::cout << "Modo de Ejecución  : " << (mode == "parallel" ? "Paralelo Cíclico (POSIX Threads)" : "Cooperativo Round-Robin (Simulación Pura)") << "\n";
        std::cout << "Hilos Concurrentes : " << num_threads << "\n";
        std::cout << "Cuantum Mínimo (Q) : " << quantum << " fila(s) por turno rotatorio\n";
        std::cout << "Conmutaciones Total: " << total_context_switches << " conmutaciones de contexto\n";
        std::cout << "Tiempo Cómputo     : " << std::fixed << std::setprecision(3)
                  << elapsed_fine.count() << " ms (" << elapsed_fine.count() / 1000.0 << " s)\n";

        // =====================================================================
        // FASE 2: VERIFICACIÓN NUMÉRICA CRUZADA CONTRA BASELINE SECUENCIAL
        // =====================================================================
        if (verify) {
            std::cout << "--------------------------------------------------------\n";
            std::cout << " Ejecutando verificación numérica contra Secuencial...\n";

            Grid2D seq_cur(width, height);
            Grid2D seq_nxt(width, height);
            init_simulation(seq_cur);
            seq_nxt = seq_cur;

            auto t_seq_start = std::chrono::high_resolution_clock::now();
            for (int s = 1; s <= total_steps; ++s) {
                step_simulation_sequential(seq_cur, seq_nxt, params);
                std::swap(seq_cur, seq_nxt);
            }
            auto t_seq_end = std::chrono::high_resolution_clock::now();
            std::chrono::duration<double, std::milli> elapsed_seq = t_seq_end - t_seq_start;

            double max_diff_v = 0.0;
            double max_diff_u = 0.0;
            for (size_t i = 0; i < grilla_final.u.size(); ++i) {
                double diff_u = std::abs(static_cast<double>(grilla_final.u[i]) - static_cast<double>(seq_cur.u[i]));
                double diff_v = std::abs(static_cast<double>(grilla_final.v[i]) - static_cast<double>(seq_cur.v[i]));
                if (diff_u > max_diff_u) max_diff_u = diff_u;
                if (diff_v > max_diff_v) max_diff_v = diff_v;
            }

            double speedup = elapsed_seq.count() / elapsed_fine.count();
            double eficiencia = (speedup / num_threads) * 100.0;

            std::cout << "Tiempo Secuencial  : " << elapsed_seq.count() << " ms\n";
            std::cout << "Speedup Obtenido   : " << std::fixed << std::setprecision(2) << speedup << "x\n";
            std::cout << "Eficiencia Paralela: " << std::fixed << std::setprecision(1) << eficiencia << " %\n";
            std::cout << "Diferencia Máx U   : " << std::scientific << max_diff_u << "\n";
            std::cout << "Diferencia Máx V   : " << std::scientific << max_diff_v << "\n";

            if (max_diff_v < 1e-4 && max_diff_u < 1e-4) {
                std::cout << "ESTADO VERIFICACIÓN: [CORRECTO / NUMÉRICAMENTE EQUIVALENTE]\n";
            } else {
                std::cout << "ESTADO VERIFICACIÓN: [ERROR - DISCREPANCIA DETECTADA]\n";
            }
        }
        std::cout << "========================================================\n";

        // Salida CSV estándar por stderr para scripts de benchmarking masivo
        std::cerr << "fine," << mode << "," << num_threads << "," << quantum << "," 
                  << width << "," << height << "," << total_steps << "," 
                  << total_context_switches << "," << elapsed_fine.count() << "\n";

    } else {
        std::cout << "[INFO Fine] Se exportaron " << frame_count << " fotogramas en " << output_dir << "\n";
    }

    return 0;
}

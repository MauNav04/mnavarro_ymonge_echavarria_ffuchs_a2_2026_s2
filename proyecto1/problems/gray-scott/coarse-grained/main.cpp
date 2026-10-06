/**
 * ============================================================================
 * Modelo de Ejecución: Coarse-Grained Multithreading (Multihilo de Grano Grueso)
 * Problema: Simulación de Reacción-Difusión de Gray-Scott (Proliferación Bacteriana)
 * ============================================================================
 *
 * Características Arquitectónicas de esta Implementación:
 * 1. Ejecución por Ráfagas Largas (Burst Execution): Cada hilo tradicional
 *    procesa un bloque continuo de filas de forma ininterrumpida, maximizando
 *    el aprovechamiento del pipeline y la localidad en memoria caché L1/L2.
 * 2. Conmutación ante Eventos de Bloqueo Costosos: El cambio de contexto / cesión
 *    del hilo ocurre ÚNICAMENTE cuando se alcanza un evento de alta latencia o
 *    bloqueo estricto (la barrera de sincronización global inter-paso o E/S en disco).
 * 3. Hilos Persistentes Tradicionales: Se implementa mediante hilos del sistema
 *    operativo (POSIX threads) con sincronización mediante barrera reutilizable.
 * 4. Doble Búfer (Double Buffering): Lectura y escritura alternadas mediante la
 *    paridad del paso (step % 2), garantizando ausencia de carreras de datos.
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
// Estructura de Contexto para cada Hilo Coarse-Grained
// ============================================================================
struct ContextoHiloCoarse {
    int id;                        // Identificador lógico del hilo [0, num_hilos - 1]
    int start_y;                   // Fila inicial de la franja (Descomposición 1D)
    int end_y;                     // Fila final de la franja
    int total_steps;               // Cantidad total de pasos de simulación
    uint64_t burst_cells;          // Celdas computadas en cada ráfaga continua
    uint64_t stall_events;         // Contador de eventos de bloqueo / sincronización
    const GrayScottParams* params; // Parámetros cinéticos del modelo Gray-Scott
    Grid2D* grid_A;                // Búfer A en memoria contigua
    Grid2D* grid_B;                // Búfer B en memoria contigua
    pthread_barrier_t* barrera;    // Barrera para el bloqueo inter-paso
    bool save_frames;              // Indicador de exportación de video PPM
    int frame_interval;            // Intervalo de pasos para captura de imágenes
    std::string output_dir;        // Directorio de salida de los fotogramas
    int* frame_counter;            // Contador global de imágenes generadas
    pthread_barrier_t* barrera_io; // Barrera para coordinar operaciones de disco (E/S)
};

// ============================================================================
// Rutina de Ejecución del Hilo Trabajador (Coarse-Grained)
// ============================================================================
void* worker_thread_coarse(void* arg) {
    ContextoHiloCoarse* ctx = static_cast<ContextoHiloCoarse*>(arg);

    // Bucle temporal: El hilo ejecuta ráfagas extensas continuas.
    // Solo se detiene / conmuta ante los eventos de bloqueo costosos (barreras y E/S).
    for (int step = 1; step <= ctx->total_steps; ++step) {
        const Grid2D& cur = (step % 2 != 0) ? *(ctx->grid_A) : *(ctx->grid_B);
        Grid2D& nxt       = (step % 2 != 0) ? *(ctx->grid_B) : *(ctx->grid_A);

        // 1. RÁFAGA CONTINUA DE CÓMPUTO (Sin interrupciones intermedias)
        // Computa todas las celdas de su bloque contiguo manteniendo las líneas
        // de caché en estado Modified/Exclusive sin ceder la CPU.
        step_simulation_slice(cur, nxt, ctx->start_y, ctx->end_y, *(ctx->params));

        // 2. EVENTO DE BLOQUEO COSTOSO 1: Barrera de Sincronización Inter-paso
        // El hilo no puede continuar hasta que todos los hilos concluyan su ráfaga.
        // Aquí ocurre el bloqueo obligatorio donde el hilo cede el pipeline.
        ctx->stall_events++;
        pthread_barrier_wait(ctx->barrera);

        // 3. EVENTO DE BLOQUEO COSTOSO 2 (Opcional): Operación de E/S en Disco
        if (ctx->save_frames && (step % ctx->frame_interval == 0)) {
            if (ctx->id == 0) {
                export_frame_ppm(nxt, (*ctx->frame_counter)++, ctx->output_dir);
            }
            // Bloqueo de espera hasta que la E/S en disco termine
            ctx->stall_events++;
            pthread_barrier_wait(ctx->barrera_io);
        }
    }

    return nullptr;
}

// ============================================================================
// Función de Entrada Principal
// ============================================================================
int main(int argc, char* argv[]) {
    // Parámetros por defecto calibrados para estabilidad y rapidez
    int width = 512;
    int height = 512;
    int total_steps = 16000;
    int num_threads = 8;        // 8 hilos por defecto (bloques grandes de granularidad gruesa)
    bool verify = false;        // Verificación cruzada celda a celda contra secuencial
    bool save_frames = false;   // Generación de fotogramas para video
    int frame_interval = 100;
    std::string output_dir = "../videos/temp_frames_coarse";

    // Procesamiento de argumentos CLI
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--threads" && i + 1 < argc) {
            num_threads = std::stoi(argv[++i]);
        } else if (arg == "--size" && i + 1 < argc) {
            width = std::stoi(argv[++i]);
            height = width;
        } else if (arg == "--steps" && i + 1 < argc) {
            total_steps = std::stoi(argv[++i]);
        } else if (arg == "--verify") {
            verify = true;
        } else if (arg == "--save-frames") {
            save_frames = true;
        } else if (arg == "--output-dir" && i + 1 < argc) {
            output_dir = argv[++i];
        }
    }

    if (num_threads < 1) num_threads = 1;

    // Estructuras de datos para la simulación
    Grid2D grid_A(width, height);
    Grid2D grid_B(width, height);
    GrayScottParams params;

    init_simulation(grid_A);
    grid_B = grid_A;

    int frame_count = 1;
    if (save_frames) {
        fs::create_directories(output_dir);
        export_frame_ppm(grid_A, 0, output_dir);
    }

    // Inicialización de barreras de sincronización
    pthread_barrier_t barrera;
    pthread_barrier_init(&barrera, nullptr, static_cast<unsigned int>(num_threads));

    pthread_barrier_t barrera_io;
    if (save_frames) {
        pthread_barrier_init(&barrera_io, nullptr, static_cast<unsigned int>(num_threads));
    }

    // Descomposición 1D: Particionado en grandes bloques contiguos de filas
    int filas_interiores = height - 2;
    int filas_base = filas_interiores / num_threads;
    int residuo = filas_interiores % num_threads;

    std::vector<pthread_t> hilos(num_threads);
    std::vector<ContextoHiloCoarse> contextos(num_threads);

    int fila_actual = 1;
    for (int t = 0; t < num_threads; ++t) {
        int filas_asignadas = filas_base + (t < residuo ? 1 : 0);
        contextos[t].id = t;
        contextos[t].start_y = fila_actual;
        contextos[t].end_y = fila_actual + filas_asignadas;
        contextos[t].total_steps = total_steps;
        contextos[t].burst_cells = static_cast<uint64_t>(filas_asignadas) * static_cast<uint64_t>(width - 2);
        contextos[t].stall_events = 0;
        contextos[t].params = &params;
        contextos[t].grid_A = &grid_A;
        contextos[t].grid_B = &grid_B;
        contextos[t].barrera = &barrera;
        contextos[t].save_frames = save_frames;
        contextos[t].frame_interval = frame_interval;
        contextos[t].output_dir = output_dir;
        contextos[t].frame_counter = &frame_count;
        contextos[t].barrera_io = save_frames ? &barrera_io : nullptr;

        fila_actual += filas_asignadas;
    }

    // Medición de alta resolución sobre el tiempo de cómputo puro
    auto start = std::chrono::high_resolution_clock::now();

    // Creación única de hilos de larga duración
    for (int t = 0; t < num_threads; ++t) {
        int rc = pthread_create(&hilos[t], nullptr, worker_thread_coarse, &contextos[t]);
        if (rc != 0) {
            std::cerr << "[ERROR CRÍTICO] Fallo al crear hilo Coarse-Grained " << t << "\n";
            return 1;
        }
    }

    // Espera de finalización de todos los pasos
    for (int t = 0; t < num_threads; ++t) {
        pthread_join(hilos[t], nullptr);
    }

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> elapsed_coarse = end - start;

    pthread_barrier_destroy(&barrera);
    if (save_frames) {
        pthread_barrier_destroy(&barrera_io);
    }

    const Grid2D& grilla_final = (total_steps % 2 != 0) ? grid_B : grid_A;

    if (!save_frames) {
        // Calcular estadísticas de ráfagas y eventos de bloqueo
        uint64_t total_stalls_hilo0 = contextos[0].stall_events;
        uint64_t burst_promedio = contextos[0].burst_cells;

        std::cout << "========================================================\n";
        std::cout << " Gray-Scott Coarse-Grained (Multihilo de Grano Grueso)\n";
        std::cout << "========================================================\n";
        std::cout << "Dimensiones        : " << width << "x" << height << "\n";
        std::cout << "Pasos Temporales   : " << total_steps << "\n";
        std::cout << "Hilos Tradicionales: " << num_threads << "\n";
        std::cout << "Ráfaga de Cómputo  : " << burst_promedio << " celdas continuas por ráfaga/hilo\n";
        std::cout << "Eventos de Bloqueo : " << total_stalls_hilo0 << " sincronizaciones (barreras)\n";
        std::cout << "Distribución Filas : ";
        for (int t = 0; t < num_threads; ++t) {
            std::cout << "[H" << t << ": y=" << contextos[t].start_y << "-" << contextos[t].end_y << "] ";
        }
        std::cout << "\n";
        std::cout << "Tiempo Cómputo     : " << std::fixed << std::setprecision(3)
                  << elapsed_coarse.count() << " ms (" << elapsed_coarse.count() / 1000.0 << " s)\n";

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

            double speedup = elapsed_seq.count() / elapsed_coarse.count();
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
        std::cerr << "coarse," << num_threads << "," << width << "," << height << "," 
                  << total_steps << "," << total_stalls_hilo0 << "," << elapsed_coarse.count() << "\n";

    } else {
        std::cout << "[INFO Coarse] Se exportaron " << frame_count << " fotogramas en " << output_dir << "\n";
    }

    return 0;
}

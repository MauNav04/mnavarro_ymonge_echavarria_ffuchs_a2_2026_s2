/**
 * ============================================================================
 * Modelo de Ejecución: SMT (Simultaneous Multithreading - Hilos Lógicos)
 * Problema: Simulación de Reacción-Difusión de Gray-Scott (Proliferación Bacteriana)
 * ============================================================================
 *
 * Características Arquitectónicas de esta Implementación:
 * 1. Descomposición de Dominio 1D: La matriz se divide horizontalmente en
 *    franjas continuas de filas [start_y, end_y) entre 16 hilos de trabajo.
 * 2. Evaluación de Concurrencia SMT:
 *    - Con SMT ON (Hardware SMT): 2 hilos lógicos coexisten en el mismo núcleo físico,
 *      compartiendo el pipeline, ALUs, FPUs y la caché L1/L2 a nivel de ciclo de reloj.
 *    - Con SMT OFF (Sobre-suscripción por Software): 16 hilos compiten sobre 8 núcleos
 *      físicos, forzando al kernel de Linux a realizar conmutaciones de contexto por software.
 * 3. Hilos Persistentes: Los hilos se crean una sola vez al inicio y se
 *    sincronizan paso a paso mediante una barrera de hardware/sistema (pthread_barrier_t).
 * 4. Doble Búfer Alternado (Double Buffering): Lecturas de 'cur' y escrituras en 'nxt'
 *    mediante la paridad del paso (step % 2), libre de cerrojos (lock-free).
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
// Detección del Estado de SMT en Linux
// ============================================================================
// Lee '/sys/devices/system/cpu/smt/control' para determinar si el hardware SMT
// se encuentra activo ('on') o deshabilitado ('off').
// ============================================================================
std::string obtener_estado_smt_linux() {
    std::ifstream archivo("/sys/devices/system/cpu/smt/control");
    std::string estado = "desconocido";
    if (archivo >> estado) {
        return estado;
    }
    return estado;
}

// Obtiene la lista de CPUs lógicos actualmente online en el sistema operativo
std::vector<int> obtener_cpus_online() {
    std::vector<int> cpus;
    long num_cpus = sysconf(_SC_NPROCESSORS_ONLN);
    if (num_cpus <= 0) num_cpus = 8;
    for (int i = 0; i < num_cpus; ++i) {
        cpus.push_back(i);
    }
    return cpus;
}

// ============================================================================
// Estructura de Contexto para cada Hilo Trabajador SMT
// ============================================================================
struct ContextoHiloSMT {
    int id;                        // Identificador lógico del hilo [0, num_hilos - 1]
    int cpu_afinidad;              // CPU asignado en la máscara de afinidad
    int start_y;                   // Fila inicial inclusiva (Descomposición 1D)
    int end_y;                     // Fila final exclusiva
    int total_steps;               // Cantidad total de iteraciones temporales
    const GrayScottParams* params; // Parámetros físicos del modelo
    Grid2D* grid_A;                // Búfer A en memoria contigua
    Grid2D* grid_B;                // Búfer B en memoria contigua
    pthread_barrier_t* barrera;    // Barrera cíclica para sincronización inter-paso
    bool save_frames;              // Si debe exportar imágenes PPM
    int frame_interval;            // Frecuencia de exportación
    std::string output_dir;        // Carpeta de salida para las imágenes
    int* frame_counter;            // Contador global de fotogramas
    pthread_barrier_t* barrera_io; // Barrera post-I/O para coordinar escritura en disco
};

// ============================================================================
// Rutina de Ejecución del Hilo Trabajador SMT
// ============================================================================
void* worker_thread_smt(void* arg) {
    ContextoHiloSMT* ctx = static_cast<ContextoHiloSMT*>(arg);

    // 1. Fijar afinidad del hilo al procesador lógico asignado
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(ctx->cpu_afinidad, &cpuset);
    int rc = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
    if (rc != 0) {
        std::cerr << "[ADVERTENCIA] No se pudo fijar afinidad SMT en CPU " << ctx->cpu_afinidad << "\n";
    }

    // 2. Bucle temporal: Cómputo del stencil mediante doble búfer alternado
    for (int step = 1; step <= ctx->total_steps; ++step) {
        const Grid2D& cur = (step % 2 != 0) ? *(ctx->grid_A) : *(ctx->grid_B);
        Grid2D& nxt       = (step % 2 != 0) ? *(ctx->grid_B) : *(ctx->grid_A);

        // Cómputo del stencil en la franja de filas asignada a este hilo SMT
        step_simulation_slice(cur, nxt, ctx->start_y, ctx->end_y, *(ctx->params));

        // Sincronización de fase 1: Todos los hilos concluyen la escritura de 'nxt'
        pthread_barrier_wait(ctx->barrera);

        // Exportación de fotograma (solo hilo 0) con sincronización de fase 2
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
// Función de Entrada Principal
// ============================================================================
int main(int argc, char* argv[]) {
    // Parámetros por defecto calibrados para estabilidad y rapidez
    int width = 512;
    int height = 512;
    int total_steps = 16000;
    int num_threads = 16;       // 16 hilos lógicos por defecto para SMT (Ryzen 7)
    bool verify = false;        // Verificación celda a celda contra secuencial
    bool save_frames = false;   // Generación de fotogramas para video
    int frame_interval = 100;
    std::string output_dir = "../videos/temp_frames_smt";

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

    // Diagnóstico del estado del hardware
    std::string smt_status = obtener_estado_smt_linux();
    std::vector<int> cpus_online = obtener_cpus_online();
    int total_cpus_online = static_cast<int>(cpus_online.size());

    // Estructuras de datos base para la simulación
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

    // Descomposición 1D: particionado equitativo de filas interiores [1, height - 1)
    int filas_interiores = height - 2;
    int filas_base = filas_interiores / num_threads;
    int residuo = filas_interiores % num_threads;

    std::vector<pthread_t> hilos(num_threads);
    std::vector<ContextoHiloSMT> contextos(num_threads);

    int fila_actual = 1;
    for (int t = 0; t < num_threads; ++t) {
        int filas_asignadas = filas_base + (t < residuo ? 1 : 0);
        contextos[t].id = t;
        // Asignación de afinidad:
        // Si SMT ON (16 CPUs online): Hilo t se fija a CPU t (Core t y su hermano SMT t+8)
        // Si SMT OFF (8 CPUs online): Hilo t se fija a CPU (t % 8), forzando sobre-suscripción
        contextos[t].cpu_afinidad = cpus_online[t % total_cpus_online];
        contextos[t].start_y = fila_actual;
        contextos[t].end_y = fila_actual + filas_asignadas;
        contextos[t].total_steps = total_steps;
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

    // Medición de tiempo de alta precisión para el cómputo puro
    auto start = std::chrono::high_resolution_clock::now();

    // Lanzamiento de hilos trabajadores persistentes
    for (int t = 0; t < num_threads; ++t) {
        int rc = pthread_create(&hilos[t], nullptr, worker_thread_smt, &contextos[t]);
        if (rc != 0) {
            std::cerr << "[ERROR CRÍTICO] Fallo al crear hilo SMT " << t << "\n";
            return 1;
        }
    }

    // Esperar culminación de los hilos
    for (int t = 0; t < num_threads; ++t) {
        pthread_join(hilos[t], nullptr);
    }

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> elapsed_smt = end - start;

    pthread_barrier_destroy(&barrera);
    if (save_frames) {
        pthread_barrier_destroy(&barrera_io);
    }

    const Grid2D& grilla_final = (total_steps % 2 != 0) ? grid_B : grid_A;

    if (!save_frames) {
        std::cout << "========================================================\n";
        std::cout << " Gray-Scott SMT (Simultaneous Multithreading)\n";
        std::cout << "========================================================\n";
        std::cout << "Dimensiones        : " << width << "x" << height << "\n";
        std::cout << "Pasos Temporales   : " << total_steps << "\n";
        std::cout << "Hilos Solicitados  : " << num_threads << "\n";
        std::cout << "Estado SMT Linux   : " << smt_status 
                  << (smt_status == "on" ? " [Hardware SMT Activo]" : " [Sobre-suscripción por Software]") << "\n";
        std::cout << "CPUs Online Detect.: " << total_cpus_online << "\n";
        std::cout << "Asignación Cores   : ";
        for (int t = 0; t < num_threads; ++t) {
            std::cout << "[H" << t << "->CPU" << contextos[t].cpu_afinidad << "] ";
        }
        std::cout << "\n";
        std::cout << "Tiempo Cómputo SMT : " << std::fixed << std::setprecision(3)
                  << elapsed_smt.count() << " ms (" << elapsed_smt.count() / 1000.0 << " s)\n";

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

            double speedup = elapsed_seq.count() / elapsed_smt.count();
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

        // Salida CSV estándar por stderr para recolección automatizada
        std::cerr << "smt," << num_threads << "," << width << "," << height << "," 
                  << total_steps << "," << smt_status << "," << elapsed_smt.count() << "\n";

    } else {
        std::cout << "[INFO SMT] Se exportaron " << frame_count << " fotogramas en " << output_dir << "\n";
    }

    return 0;
}

/**
 * ============================================================================
 * Modelo de Ejecución: CMP (Chip Multiprocessing - Paralelismo Real)
 * Problema: Simulación de Reacción-Difusión de Gray-Scott (Proliferación Bacteriana)
 * ============================================================================
 *
 * Características Arquitectónicas de esta Implementación:
 * 1. Descomposición de Dominio 1D: La matriz se divide horizontalmente en
 *    franjas continuas de filas [start_y, end_y) entre los hilos de trabajo.
 * 2. Localidad Espacial y Caché L1/L2: Al recorrer la memoria en orden Row-Major,
 *    se aprovecha al máximo el Hardware Prefetcher de cada núcleo físico y se
 *    evita el falso compartimiento (False Sharing) en la escritura de 'next'.
 * 3. Hilos Persistentes: Los hilos se crean una única vez al inicio y se
 *    sincronizan paso a paso mediante una barrera de hardware/sistema (pthread_barrier_t).
 * 4. Afinidad a Núcleos Físicos: Cada hilo se fija a un núcleo físico independiente
 *    mediante pthread_setaffinity_np para aislar el paralelismo CMP puro de SMT.
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
#include <set>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <filesystem>

namespace fs = std::filesystem;

// ============================================================================
// Detección Dinámica de Núcleos Físicos de la CPU (Linux sysfs)
// ============================================================================
// En procesadores AMD Zen 3 (como el Ryzen 7 5700G y Ryzen 7 7730U el cual es el de prueba final):
// Los CPUs 0 a 7 corresponden a núcleos físicos independientes (Core 0 a Core 7).
// Los CPUs 8 a 15 corresponden a los hermanos lógicos SMT (Hyper-Threading).
// Esta función lee '/sys/devices/system/cpu/cpuX/topology/core_id' para garantizar
// portabilidad en cualquier máquina Linux con topologías heterogéneas.
// ============================================================================
std::vector<int> obtener_cpus_fisicos() {
    std::vector<int> cpus_fisicos;
    std::set<int> core_ids_vistos;
    unsigned int total_logicos = std::thread::hardware_concurrency();
    if (total_logicos == 0) total_logicos = 8;

    for (unsigned int cpu = 0; cpu < total_logicos; ++cpu) {
        std::string ruta = "/sys/devices/system/cpu/cpu" + std::to_string(cpu) + "/topology/core_id";
        std::ifstream archivo(ruta);
        int core_id = -1;
        if (archivo >> core_id) {
            if (core_ids_vistos.find(core_id) == core_ids_vistos.end()) {
                core_ids_vistos.insert(core_id);
                cpus_fisicos.push_back(static_cast<int>(cpu));
            }
        } else {
            // En caso de que sysfs no esté disponible, usar orden lineal básico
            cpus_fisicos.push_back(static_cast<int>(cpu));
        }
    }

    // Si el usuario solicita más hilos que núcleos físicos disponibles,
    // rellenar con los procesadores lógicos restantes (hermanos SMT)
    for (unsigned int cpu = 0; cpu < total_logicos; ++cpu) {
        if (std::find(cpus_fisicos.begin(), cpus_fisicos.end(), static_cast<int>(cpu)) == cpus_fisicos.end()) {
            cpus_fisicos.push_back(static_cast<int>(cpu));
        }
    }

    return cpus_fisicos;
}

// ============================================================================
// Estructura de Contexto para cada Hilo Trabajador (Worker Thread)
// ============================================================================
struct ContextoHiloCMP {
    int id;                     // Identificador lógico del hilo [0, num_hilos - 1]
    int cpu_afinidad;           // CPU lógico/físico asignado en la máscara de afinidad
    int start_y;                // Fila inicial inclusiva asignada (Descomposición 1D)
    int end_y;                  // Fila final exclusiva asignada
    int total_steps;            // Cantidad de iteraciones temporales a computar
    const GrayScottParams* params; // Parámetros cinéticos y de difusión del modelo
    Grid2D* grid_A;             // Búfer A en memoria contigua
    Grid2D* grid_B;             // Búfer B en memoria contigua
    pthread_barrier_t* barrera; // Barrera cíclica compartida para sincronización de cómputo
    bool save_frames;           // Indica si se deben exportar imágenes PPM
    int frame_interval;         // Cada cuántos pasos exportar
    std::string output_dir;     // Directorio de salida para las imágenes
    int* frame_counter;         // Contador atómico/compartido de fotogramas
    pthread_barrier_t* barrera_io; // Barrera para asegurar que la I/O termine antes de alterar buffers
};

// ============================================================================
// Rutina de Ejecución del Hilo Trabajador (Worker Thread)
// ============================================================================
void* worker_thread_cmp(void* arg) {
    ContextoHiloCMP* ctx = static_cast<ContextoHiloCMP*>(arg);

    // 1. Fijar afinidad del hilo al núcleo físico asignado
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(ctx->cpu_afinidad, &cpuset);
    int rc = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
    if (rc != 0) {
        std::cerr << "[ADVERTENCIA] No se pudo fijar afinidad en CPU " << ctx->cpu_afinidad << "\n";
    }

    // 2. Bucle temporal: Cómputo de la simulación mediante doble búfer alternado
    // Al alternar lecturas y escrituras según si el paso es par o impar,
    // eliminamos la necesidad de punteros compartidos protegidos por cerrojos.
    for (int step = 1; step <= ctx->total_steps; ++step) {
        const Grid2D& cur = (step % 2 != 0) ? *(ctx->grid_A) : *(ctx->grid_B);
        Grid2D& nxt       = (step % 2 != 0) ? *(ctx->grid_B) : *(ctx->grid_A);

        // Cómputo independiente del stencil en la franja de filas asignada
        step_simulation_slice(cur, nxt, ctx->start_y, ctx->end_y, *(ctx->params));

        // Sincronización 1: Todos los hilos deben terminar de calcular y escribir en 'nxt'
        pthread_barrier_wait(ctx->barrera);

        // Si se solicitó exportar video, el hilo 0 guarda el fotograma y los demás esperan
        if (ctx->save_frames && (step % ctx->frame_interval == 0)) {
            if (ctx->id == 0) {
                export_frame_ppm(nxt, (*ctx->frame_counter)++, ctx->output_dir);
            }
            // Sincronización 2: Nadie inicia el siguiente paso hasta que la escritura a disco termine
            pthread_barrier_wait(ctx->barrera_io);
        }
    }

    return nullptr;
}

// ============================================================================
// Función de Entrada Principal
// ============================================================================
int main(int argc, char* argv[]) {
    // Parámetros por defecto
    int width = 512;
    int height = 512;
    int total_steps = 16000;
    int num_threads = 8;        // 8 hilos físicos por defecto (Ryzen 7)
    bool verify = false;        // Verificación numérica celda a celda contra secuencial
    bool save_frames = false;   // Modo exportación de video PPM
    int frame_interval = 100;
    std::string output_dir = "../videos/temp_frames_cmp";

    // Procesamiento de argumentos de línea de comandos
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

    // Obtener lista ordenada de CPUs físicos del procesador
    std::vector<int> cpus_disponibles = obtener_cpus_fisicos();

    // Estructuras de datos para el doble búfer
    Grid2D grid_A(width, height);
    Grid2D grid_B(width, height);
    GrayScottParams params;

    init_simulation(grid_A);
    grid_B = grid_A; // Copiar bordes constantes y estado inicial

    // Preparación para exportación de video si fue solicitada
    int frame_count = 1;
    if (save_frames) {
        fs::create_directories(output_dir);
        export_frame_ppm(grid_A, 0, output_dir);
    }

    // Inicializar las barreras POSIX
    pthread_barrier_t barrera;
    pthread_barrier_init(&barrera, nullptr, static_cast<unsigned int>(num_threads));

    pthread_barrier_t barrera_io;
    if (save_frames) {
        pthread_barrier_init(&barrera_io, nullptr, static_cast<unsigned int>(num_threads));
    }

    // Particionado 1D de las filas interiores [1, height - 1)
    int filas_interiores = height - 2;
    int filas_base = filas_interiores / num_threads;
    int residuo = filas_interiores % num_threads;

    std::vector<pthread_t> hilos(num_threads);
    std::vector<ContextoHiloCMP> contextos(num_threads);

    int fila_actual = 1;
    for (int t = 0; t < num_threads; ++t) {
        int filas_asignadas = filas_base + (t < residuo ? 1 : 0);
        contextos[t].id = t;
        // Asignar CPU físico según lista detectada
        contextos[t].cpu_afinidad = cpus_disponibles[t % cpus_disponibles.size()];
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

    // Medición de alta resolución sobre el tiempo de cómputo multihilo
    auto start = std::chrono::high_resolution_clock::now();

    // Lanzamiento de hilos trabajadores persistentes
    for (int t = 0; t < num_threads; ++t) {
        int rc = pthread_create(&hilos[t], nullptr, worker_thread_cmp, &contextos[t]);
        if (rc != 0) {
            std::cerr << "[ERROR CRÍTICO] Fallo al crear hilo " << t << "\n";
            return 1;
        }
    }

    // Esperar la culminación de todos los pasos temporales
    for (int t = 0; t < num_threads; ++t) {
        pthread_join(hilos[t], nullptr);
    }

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> elapsed_cmp = end - start;

    // Liberar recursos de las barreras
    pthread_barrier_destroy(&barrera);
    if (save_frames) {
        pthread_barrier_destroy(&barrera_io);
    }

    // Identificar la grilla con el estado final
    const Grid2D& grilla_final = (total_steps % 2 != 0) ? grid_B : grid_A;

    if (!save_frames) {
        // Banner informativo en consola
        std::cout << "========================================================\n";
        std::cout << " Gray-Scott CMP (Chip Multiprocessing - Paralelismo Real)\n";
        std::cout << "========================================================\n";
        std::cout << "Dimensiones        : " << width << "x" << height << "\n";
        std::cout << "Pasos Temporales   : " << total_steps << "\n";
        std::cout << "Hilos Físicos (CMP): " << num_threads << "\n";
        std::cout << "Asignación Cores   : ";
        for (int t = 0; t < num_threads; ++t) {
            std::cout << "[H" << t << "->CPU" << contextos[t].cpu_afinidad << " (y:" 
                      << contextos[t].start_y << "-" << contextos[t].end_y << ")] ";
        }
        std::cout << "\n";
        std::cout << "Tiempo Cómputo CMP : " << std::fixed << std::setprecision(3)
                  << elapsed_cmp.count() << " ms (" << elapsed_cmp.count() / 1000.0 << " s)\n";

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

            // Comparar celda por celda la concentración bacteriana V
            double max_diff_v = 0.0;
            double max_diff_u = 0.0;
            for (size_t i = 0; i < grilla_final.u.size(); ++i) {
                double diff_u = std::abs(static_cast<double>(grilla_final.u[i]) - static_cast<double>(seq_cur.u[i]));
                double diff_v = std::abs(static_cast<double>(grilla_final.v[i]) - static_cast<double>(seq_cur.v[i]));
                if (diff_u > max_diff_u) max_diff_u = diff_u;
                if (diff_v > max_diff_v) max_diff_v = diff_v;
            }

            double speedup = elapsed_seq.count() / elapsed_cmp.count();
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
        std::cerr << "cmp," << num_threads << "," << width << "," << height << "," 
                  << total_steps << "," << elapsed_cmp.count() << "\n";
    } else {
        std::cout << "[INFO CMP] Se exportaron " << frame_count << " fotogramas en " << output_dir << "\n";
    }

    return 0;
}

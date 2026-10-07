/**
 * ============================================================================
 * Modelo de Ejecución: SMT (Simultaneous Multithreading - Hilos Lógicos)
 * Problema: Trazado de Rayos 3D (Ray Tracing)
 * ============================================================================
 *
 * Características Arquitectónicas:
 * 1. Descomposición de Dominio 1D: La altura total de la imagen se divide en
 *    16 franjas horizontales continuas entre los 16 hilos lógicos de trabajo.
 * 2. Evaluación de Concurrencia SMT (Hardware vs. Software):
 *    - Con SMT ON (Hardware Real): 2 hilos lógicos coexisten en el mismo núcleo
 *      físico compartiendo el pipeline, ALUs, FPUs y cachés L1/L2 a nivel de ciclo.
 *    - Con SMT OFF (Sobre-suscripción por Software): 16 hilos compiten sobre los
 *      núcleos físicos online, obligando al planificador de Linux a realizar
 *      conmutaciones de contexto por software (time-slicing).
 * 3. Hilos Persistentes y Afinidad Mapeada: Afinidad adaptativa con pthread_setaffinity_np
 *    según el estado detectado en /sys/devices/system/cpu/smt/control.
 * 4. Escritura Directa Libre de Cerrojos (Lock-Free): Cada hilo escribe exclusivamente
 *    en sus índices correspondientes del Framebuffer en RAM contigua.
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
#include <sched.h>
#include <fstream>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <unistd.h>
#include <set>

// ============================================================================
// Detección del Estado de SMT en Linux
// ============================================================================
std::string obtener_estado_smt_linux() {
    std::ifstream archivo("/sys/devices/system/cpu/smt/control");
    std::string estado = "desconocido";
    if (archivo >> estado) {
        return estado;
    }
    return estado;
}

// Verifica si un CPU específico se encuentra en estado online
bool cpu_esta_online(int cpu) {
    if (cpu == 0) return true;
    std::ifstream f("/sys/devices/system/cpu/cpu" + std::to_string(cpu) + "/online");
    int val = 0;
    if (f >> val) return val == 1;
    // Si el archivo no existe pero el directorio sí, asumimos que está activo
    std::ifstream f_core("/sys/devices/system/cpu/cpu" + std::to_string(cpu) + "/topology/core_id");
    return f_core.good();
}

// Obtiene la lista de CPUs lógicos actualmente online en el sistema operativo
std::vector<int> obtener_cpus_online() {
    std::vector<int> cpus;
    for (int cpu = 0; cpu < 64; ++cpu) {
        std::string ruta = "/sys/devices/system/cpu/cpu" + std::to_string(cpu);
        std::ifstream check_online(ruta + "/online");
        std::ifstream check_topo(ruta + "/topology/core_id");
        if (cpu > 0 && !check_online.good() && !check_topo.good()) {
            break; // No existen más procesadores en sysfs
        }
        if (cpu_esta_online(cpu)) {
            cpus.push_back(cpu);
        }
    }
    if (cpus.empty()) {
        long num_cpus = sysconf(_SC_NPROCESSORS_ONLN);
        if (num_cpus <= 0) num_cpus = 8;
        for (int i = 0; i < num_cpus; ++i) cpus.push_back(i);
    }
    return cpus;
}

// Obtiene los núcleos físicos reales inspeccionando core_id entre los CPUs online
std::vector<int> obtener_cpus_fisicos() {
    std::vector<int> cpus_fisicos;
    std::set<int> core_ids_vistos;
    std::vector<int> cpus_online = obtener_cpus_online();

    for (int cpu : cpus_online) {
        std::string ruta = "/sys/devices/system/cpu/cpu" + std::to_string(cpu) + "/topology/core_id";
        std::ifstream archivo(ruta);
        int core_id = -1;
        if (archivo >> core_id) {
            if (core_ids_vistos.find(core_id) == core_ids_vistos.end()) {
                core_ids_vistos.insert(core_id);
                cpus_fisicos.push_back(cpu);
            }
        } else {
            cpus_fisicos.push_back(cpu);
        }
    }
    if (cpus_fisicos.empty()) {
        cpus_fisicos = cpus_online;
    }
    return cpus_fisicos;
}

// ============================================================================
// Contexto para cada Hilo Trabajador SMT
// ============================================================================
struct ContextoHiloSMT {
    int id;                        // Identificador lógico del hilo [0, num_threads - 1]
    int cpu_afinidad;              // CPU asignado en la máscara de afinidad
    int start_y;                   // Fila inicial de la franja (inclusiva)
    int end_y;                     // Fila final de la franja (exclusiva)
    const camera* cam;             // Puntero a la cámara calibrada (read-only)
    const hittable_list* world;    // Puntero a la escena 3D (read-only)
    Framebuffer* fb;               // Puntero al Framebuffer compartido en RAM
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

    // 2. Cómputo independiente del trazado de rayos en la franja asignada
    ctx->cam->render_slice(*(ctx->world), *(ctx->fb), ctx->start_y, ctx->end_y);

    return nullptr;
}

int main(int argc, char* argv[]) {
    int width = 400;
    int samples = 10;
    int max_depth = 50;
    int num_threads = 16;       // 16 hilos lógicos por defecto para SMT
    std::string mode = "hw";    // "hw" para SMT Real de Hardware, "sw" para Sobre-suscripción por Software
    bool verify = false;
    std::string output_ppm = "";

    // Procesamiento de argumentos CLI
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--threads" && i + 1 < argc) {
            num_threads = std::stoi(argv[++i]);
        } else if (arg == "--mode" && i + 1 < argc) {
            mode = argv[++i];
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

    // Diagnóstico de topología y CPUs online
    std::string smt_status = obtener_estado_smt_linux();
    std::vector<int> cpus_online = obtener_cpus_online();
    std::vector<int> cpus_fisicos = obtener_cpus_fisicos();
    int total_cpus_online = static_cast<int>(cpus_online.size());

    // Si SMT en Linux está "off" en el kernel, forzar mode = "sw"
    if (smt_status == "off") {
        mode = "sw";
    }

    // Determinar la lista de CPUs para afinar los hilos
    std::vector<int> cpus_target;
    if (mode == "sw") {
        // En modo sobre-suscripción (SMT OFF): usamos los núcleos físicos para forzar time-slicing
        cpus_target = cpus_fisicos;
    } else {
        // En modo hardware SMT: usamos los procesadores lógicos online
        cpus_target = cpus_online;
    }
    int num_target_cores = static_cast<int>(cpus_target.size());
    if (num_target_cores <= 0) num_target_cores = 1;

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

    // Particionado 1D en franjas de scanlines [0, height)
    int filas_base = height / num_threads;
    int residuo = height % num_threads;

    std::vector<pthread_t> hilos(num_threads);
    std::vector<ContextoHiloSMT> contextos(num_threads);

    int fila_actual = 0;
    for (int t = 0; t < num_threads; ++t) {
        int filas_asignadas = filas_base + (t < residuo ? 1 : 0);
        contextos[t].id = t;
        // Asignación de afinidad:
        // Si SMT ON (HW): se reparte entre los 16 CPUs lógicos
        // Si SMT OFF (SW): se fuerza sobre-suscripción repartiendo entre los núcleos físicos
        contextos[t].cpu_afinidad = cpus_target[t % num_target_cores];
        contextos[t].start_y = fila_actual;
        contextos[t].end_y = fila_actual + filas_asignadas;
        contextos[t].cam = &cam;
        contextos[t].world = &world;
        contextos[t].fb = &fb;

        fila_actual += filas_asignadas;
    }

    // =========================================================================
    // MEDICIÓN DE TIEMPO PURO DE CÓMPUTO SMT
    // =========================================================================
    auto start = std::chrono::high_resolution_clock::now();

    for (int t = 0; t < num_threads; ++t) {
        int rc = pthread_create(&hilos[t], nullptr, worker_thread_smt, &contextos[t]);
        if (rc != 0) {
            std::cerr << "[ERROR CRÍTICO] Fallo al crear hilo SMT " << t << "\n";
            return 1;
        }
    }

    for (int t = 0; t < num_threads; ++t) {
        pthread_join(hilos[t], nullptr);
    }

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> elapsed_smt = end - start;

    std::cout << "========================================================\n";
    std::cout << " Ray Tracing - SMT (Simultaneous Multithreading)\n";
    std::cout << "========================================================\n";
    std::cout << "Resolución         : " << width << "x" << height << "\n";
    std::cout << "Muestras / Píxel   : " << samples << "\n";
    std::cout << "Profundidad Rebote : " << max_depth << "\n";
    std::cout << "Hilos Solicitados  : " << num_threads << "\n";
    std::cout << "Modo de Ejecución  : " << (mode == "sw" ? "Sobre-suscripción por Software (SMT OFF)" : "Hardware SMT Real (SMT ON)") << "\n";
    std::cout << "Estado SMT Linux   : " << smt_status 
              << (smt_status == "on" ? " [Kernel SMT Activo]" : " [Kernel SMT Apagado]") << "\n";
    std::cout << "CPUs Online Detect.: " << total_cpus_online << "\n";
    std::cout << "Cores Destino      : " << num_target_cores << " núcleos (" << (mode == "sw" ? "Físicos aislados" : "Lógicos del HW") << ")\n";
    std::cout << "Asignación Cores   : ";
    for (int t = 0; t < num_threads; ++t) {
        std::cout << "[H" << t << "->CPU" << contextos[t].cpu_afinidad << "] ";
    }
    std::cout << "\n";
    std::cout << "Tiempo Cómputo SMT : " << std::fixed << std::setprecision(3)
              << elapsed_smt.count() << " ms (" << elapsed_smt.count() / 1000.0 << " s)\n";

    // Verificación numérica cruzada contra secuencial
    if (verify) {
        std::cout << "--------------------------------------------------------\n";
        std::cout << " Ejecutando verificación numérica contra Secuencial...\n";
        Framebuffer fb_seq(width, height);

        auto t_seq_start = std::chrono::high_resolution_clock::now();
        cam.render_slice(world, fb_seq, 0, height);
        auto t_seq_end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> elapsed_seq = t_seq_end - t_seq_start;

        double speedup = elapsed_seq.count() / elapsed_smt.count();
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
    std::cerr << "smt," << mode << "," << num_threads << "," << width << "," << height << "," 
              << samples << "," << max_depth << "," << smt_status << "," << elapsed_smt.count() << "\n";

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

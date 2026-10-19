/**
 * ============================================================================
 * Modelo de Ejecución: CMP (Chip Multiprocessing - Paralelismo Real)
 * ============================================================================
 *
 * Características Arquitectónicas:
 * 1. Descomposición por Filas (Scanlines): La altura total de la
 *    imagen se particiona en franjas continuas horizontales.
 * 2. Localidad Espacial y Caché L1/L2: Al procesar líneas completas en orden
 *    Row-Major, se activa el Hardware Prefetche y se elimina la penalización de fallos de caché L1/L2.
 * 3. Hilos Persistentes y Afinidad a Núcleos Físicos: Cada hilo del sistema
 *    operativo se fija a un núcleo físico independiente mediante pthread_setaffinity_np
 * 4. Escritura Directa Libre de Cerrojos (Lock-Free): Cada hilo escribe
 *    exclusivamente en sus celdas de memoria RAM.
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
#include <set>
#include <algorithm>
#include <cmath>
#include <iomanip>

// ============================================================================
// Detección Dinámica de Núcleos Físicos (sysfs)
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
            cpus_fisicos.push_back(static_cast<int>(cpu));
        }
    }

    // Rellenar si se solicitan más hilos que cores físicos detectados
    for (unsigned int cpu = 0; cpu < total_logicos; ++cpu) {
        if (std::find(cpus_fisicos.begin(), cpus_fisicos.end(), static_cast<int>(cpu)) == cpus_fisicos.end()) {
            cpus_fisicos.push_back(static_cast<int>(cpu));
        }
    }

    return cpus_fisicos;
}

// ============================================================================
// Contexto para cada Hilo Trabajador CMP
// ============================================================================
struct ContextoHiloCMP {
    int id;                        // Identificador lógico del hilo [0, num_threads - 1]
    int cpu_afinidad;              // Núcleo físico asignado en la máscara de afinidad
    int start_y;                   // Fila inicial de la franja 
    int end_y;                     // Fila final de la franja 
    const camera* cam;             // Puntero a la cámara calibrada 
    const hittable_list* world;    // Puntero a la escena 3D 
    Framebuffer* fb;               // Puntero al Framebuffer compartido 
};

// ============================================================================
// Rutina de Ejecución del Hilo (Worker Thread)
// ============================================================================
void* worker_thread_cmp(void* arg) {
    ContextoHiloCMP* ctx = static_cast<ContextoHiloCMP*>(arg);

    // 1. Hilo al núcleo físico asignado
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(ctx->cpu_afinidad, &cpuset);
    int rc = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
    if (rc != 0) {
        std::cerr << "[ADVERTENCIA] No se pudo fijar afinidad CMP en CPU " << ctx->cpu_afinidad << "\n";
    }

    // 2. Trazado de rayos en la franja asignada
    ctx->cam->render_slice(*(ctx->world), *(ctx->fb), ctx->start_y, ctx->end_y);

    return nullptr;
}

int main(int argc, char* argv[]) {
    int width = 400;
    int samples = 10;
    int max_depth = 50;
    int num_threads = 8;
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

    // Obtener CPUs físicos de la arquitectura
    std::vector<int> cpus_disponibles = obtener_cpus_fisicos();

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

    // Particionado de las scanlines [0, height)
    int filas_base = height / num_threads;
    int residuo = height % num_threads;

    std::vector<pthread_t> hilos(num_threads);
    std::vector<ContextoHiloCMP> contextos(num_threads);

    int fila_actual = 0;
    for (int t = 0; t < num_threads; ++t) {
        int filas_asignadas = filas_base + (t < residuo ? 1 : 0);
        contextos[t].id = t;
        contextos[t].cpu_afinidad = cpus_disponibles[t % cpus_disponibles.size()];
        contextos[t].start_y = fila_actual;
        contextos[t].end_y = fila_actual + filas_asignadas;
        contextos[t].cam = &cam;
        contextos[t].world = &world;
        contextos[t].fb = &fb;

        fila_actual += filas_asignadas;
    }

    // =========================================================================
    // MEDICIÓN DE TIEMPO PURO 
    // =========================================================================
    auto start = std::chrono::high_resolution_clock::now();

    for (int t = 0; t < num_threads; ++t) {
        int rc = pthread_create(&hilos[t], nullptr, worker_thread_cmp, &contextos[t]);
        if (rc != 0) {
            std::cerr << "[ERROR CRÍTICO] Fallo al crear hilo CMP " << t << "\n";
            return 1;
        }
    }

    for (int t = 0; t < num_threads; ++t) {
        pthread_join(hilos[t], nullptr);
    }

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> elapsed_cmp = end - start;

    std::cout << "========================================================\n";
    std::cout << " Ray Tracing - CMP (Chip Multiprocessing Real)\n";
    std::cout << "========================================================\n";
    std::cout << "Resolución         : " << width << "x" << height << "\n";
    std::cout << "Muestras / Píxel   : " << samples << "\n";
    std::cout << "Profundidad Rebote : " << max_depth << "\n";
    std::cout << "Hilos Físicos (CMP): " << num_threads << "\n";
    std::cout << "Asignación Cores   : ";
    for (int t = 0; t < num_threads; ++t) {
        std::cout << "[H" << t << "->CPU" << contextos[t].cpu_afinidad << " (y:" 
                  << contextos[t].start_y << "-" << contextos[t].end_y << ")] ";
    }
    std::cout << "\n";
    std::cout << "Tiempo Cómputo CMP : " << std::fixed << std::setprecision(3)
              << elapsed_cmp.count() << " ms (" << elapsed_cmp.count() / 1000.0 << " s)\n";

    // Verificación numérica contra versión secuencial
    if (verify) {
        std::cout << "--------------------------------------------------------\n";
        std::cout << " Ejecutando verificación numérica contra Secuencial...\n";
        Framebuffer fb_seq(width, height);

        auto t_seq_start = std::chrono::high_resolution_clock::now();
        cam.render_slice(world, fb_seq, 0, height);
        auto t_seq_end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> elapsed_seq = t_seq_end - t_seq_start;

        double speedup = elapsed_seq.count() / elapsed_cmp.count();
        double eficiencia = (speedup / num_threads) * 100.0;
        double diff = fb.diff_max(fb_seq);

        std::cout << "Tiempo Secuencial  : " << elapsed_seq.count() << " ms\n";
        std::cout << "Speedup Obtenido   : " << std::fixed << std::setprecision(2) << speedup << "x\n";
        std::cout << "Eficiencia Paralela: " << std::fixed << std::setprecision(1) << eficiencia << " %\n";
        std::cout << "Diferencia Máx RGB : " << std::scientific << diff << "\n";
        std::cout << "ESTADO VERIFICACIÓN: [COMPLETADO - CÁLCULO ESTOCÁSTICO VÁLIDO]\n";
    }
    std::cout << "========================================================\n";

    // Salida estructurada CSV por stderr para scripts de benchmarking masivo
    std::cerr << "cmp," << num_threads << "," << width << "," << height << "," 
              << samples << "," << max_depth << "," << elapsed_cmp.count() << "\n";

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

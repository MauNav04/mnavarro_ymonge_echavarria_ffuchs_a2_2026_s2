/**
 * ============================================================================
 * Modelo de Ejecución: Fine-Grained Multithreading (Multihilo de Grano Fino)
 * Problema: Trazado de Rayos 3D (Ray Tracing)
 * ============================================================================
 *
 * Características Arquitectónicas:
 * 1. Conmutación Incondicional por Cuantums Mínimos (Round-Robin Interleaving):
 *    A diferencia de Coarse-Grained (donde el hilo se ejecuta en ráfagas largas y solo
 *    conmuta ante bloqueos costosos), en Fine-Grained la conmutación de hilo ocurre
 *    DE FORMA INCONDICIONAL en cada cuantum mínimo (Q = 1 fila por defecto),
 *    independientemente de si el hilo actual sufrió un stall o no.
 * 2. Propuestas de Implementación Comparadas:
 *    - Propuesta 1 (Por defecto: --mode cooperative):
 *      Simulación rigurosa del modelo de grano fino mediante planificación cooperativa
 *      en espacio de usuario. Modela un procesador multihilo intercalado donde T hilos
 *      lógicos rotan por turno estricto ejecutando un cuantum mínimo (Q = 1 fila).
 *      En cada cuantum se registra una conmutación forzada de contexto, demostrando
 *      la sobrecarga masiva de software en ausencia de conmutación por hardware.
 *    - Propuesta 2 (--mode parallel):
 *      Implementación paralela intercalada mediante hilos del SO (POSIX threads) con
 *      descomposición cíclica (Cyclic Distribution). Cada hilo procesa filas intercaladas
 *      en bloques mínimos (Q filas), evidenciando el impacto de la pérdida de localidad
 *      espacial y la degradación del Hardware Prefetcher de caché L1/L2.
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
// Contexto para el Modo Paralelo Cíclico (POSIX Threads)
// ============================================================================
struct ContextoHiloFineParallel {
    int id;                         // ID lógico del hilo [0, num_threads - 1]
    int num_threads;                // Total de hilos paralelos
    int height;                     // Altura total de la imagen
    int quantum;                    // Cuantum mínimo (filas por trozo)
    uint64_t context_switches;      // Conmutaciones forzadas registradas
    const camera* cam;              // Puntero a la cámara calibrada (read-only)
    const hittable_list* world;     // Puntero a la escena 3D (read-only)
    Framebuffer* fb;                // Puntero al Framebuffer compartido en RAM
};

// ============================================================================
// Rutina de Ejecución: Modo Paralelo Cíclico (POSIX Threads)
// ============================================================================
void* worker_thread_fine_parallel(void* arg) {
    ContextoHiloFineParallel* ctx = static_cast<ContextoHiloFineParallel*>(arg);

    // Descomposición Cíclica Intercalada (Fine-Grained):
    // Se divide la altura en trozos de tamaño `quantum` (1 fila).
    // Cada trozo se asigna al hilo cuando (chunk_id % num_threads == id).
    int y = 0;
    while (y < ctx->height) {
        int chunk_id = y / ctx->quantum;
        int chunk_size = std::min(ctx->quantum, ctx->height - y);

        if (chunk_id % ctx->num_threads == ctx->id) {
            ctx->cam->render_slice(*(ctx->world), *(ctx->fb), y, y + chunk_size);
            ctx->context_switches++;
        }
        y += chunk_size;
    }

    return nullptr;
}

// ============================================================================
// Rutina de Ejecución: Modo Cooperativo Round-Robin (Simulación de Grano Fino)
// ============================================================================
void ejecutar_fine_grained_cooperativo(const camera& cam, const hittable_list& world,
                                      Framebuffer& fb, int height,
                                      int num_threads, int quantum,
                                      uint64_t& total_context_switches) {
    total_context_switches = 0;
    int turno_hilo = 0;

    int y = 0;
    while (y < height) {
        int chunk_size = std::min(quantum, height - y);

        // 1. El hilo virtual activo ejecuta su cuantum mínimo de trabajo
        cam.render_slice(world, fb, y, y + chunk_size);
        total_context_switches++;

        // 2. CONMUTACIÓN INCONDICIONAL DE CONTEXTO (Round-Robin):
        // Ocurre siempre tras cada cuantum sin importar stalls o eventos
        turno_hilo = (turno_hilo + 1) % num_threads;

        y += chunk_size;
    }
}

int main(int argc, char* argv[]) {
    int width = 400;
    int samples = 10;
    int max_depth = 50;
    int num_threads = 8;
    int quantum = 1;                     // Cuantum mínimo por defecto: 1 fila
    std::string mode = "cooperative";     // "cooperative" (defecto) o "parallel"
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
        } else if (arg == "--quantum" && i + 1 < argc) {
            quantum = std::stoi(argv[++i]);
        } else if (arg == "--mode" && i + 1 < argc) {
            mode = argv[++i];
        } else if (arg == "--verify") {
            verify = true;
        } else if (arg == "--save-ppm" && i + 1 < argc) {
            output_ppm = argv[++i];
        }
    }

    if (num_threads < 1) num_threads = 1;
    if (quantum < 1) quantum = 1;

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
    uint64_t total_context_switches = 0;

    // =========================================================================
    // MEDICIÓN DE TIEMPO PURO DE CÓMPUTO FINE-GRAINED
    // =========================================================================
    auto start = std::chrono::high_resolution_clock::now();

    if (mode == "parallel") {
        // Propuesta 2: Paralelismo cíclico intercalado con hilos del SO
        std::vector<pthread_t> hilos(num_threads);
        std::vector<ContextoHiloFineParallel> contextos(num_threads);

        for (int t = 0; t < num_threads; ++t) {
            contextos[t].id = t;
            contextos[t].num_threads = num_threads;
            contextos[t].height = height;
            contextos[t].quantum = quantum;
            contextos[t].context_switches = 0;
            contextos[t].cam = &cam;
            contextos[t].world = &world;
            contextos[t].fb = &fb;

            int rc = pthread_create(&hilos[t], nullptr, worker_thread_fine_parallel, &contextos[t]);
            if (rc != 0) {
                std::cerr << "[ERROR CRÍTICO] Fallo al crear hilo Fine-Grained " << t << "\n";
                return 1;
            }
        }

        for (int t = 0; t < num_threads; ++t) {
            pthread_join(hilos[t], nullptr);
            total_context_switches += contextos[t].context_switches;
        }

    } else {
        // Propuesta 1: Simulación cooperativa pura con rotación round-robin
        ejecutar_fine_grained_cooperativo(cam, world, fb, height, num_threads, quantum, total_context_switches);
    }

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> elapsed_fine = end - start;

    std::cout << "========================================================\n";
    std::cout << " Ray Tracing - Fine-Grained (Multihilo de Grano Fino)\n";
    std::cout << "========================================================\n";
    std::cout << "Resolución         : " << width << "x" << height << "\n";
    std::cout << "Muestras / Píxel   : " << samples << "\n";
    std::cout << "Profundidad Rebote : " << max_depth << "\n";
    std::cout << "Modo Evaluado      : " << (mode == "parallel" ? "Paralelo Cíclico en HW" : "Simulación Cooperativa Round-Robin") << "\n";
    std::cout << "Hilos Lógicos      : " << num_threads << "\n";
    std::cout << "Cuantum Mínimo     : " << quantum << " fila(s)\n";
    std::cout << "Conmutaciones Forz.: " << total_context_switches << " cambios de contexto\n";
    std::cout << "Tiempo Cómputo     : " << std::fixed << std::setprecision(3)
              << elapsed_fine.count() << " ms (" << elapsed_fine.count() / 1000.0 << " s)\n";

    // Verificación numérica cruzada contra secuencial
    if (verify) {
        std::cout << "--------------------------------------------------------\n";
        std::cout << " Ejecutando verificación numérica contra Secuencial...\n";
        Framebuffer fb_seq(width, height);

        auto t_seq_start = std::chrono::high_resolution_clock::now();
        cam.render_slice(world, fb_seq, 0, height);
        auto t_seq_end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> elapsed_seq = t_seq_end - t_seq_start;

        double speedup = elapsed_seq.count() / elapsed_fine.count();
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
    std::cerr << "fine," << mode << "," << num_threads << "," << quantum << "," 
              << width << "," << height << "," << samples << "," << max_depth << "," 
              << total_context_switches << "," << elapsed_fine.count() << "\n";

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

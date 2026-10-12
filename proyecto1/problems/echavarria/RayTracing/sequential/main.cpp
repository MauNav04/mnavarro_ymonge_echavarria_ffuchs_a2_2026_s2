/**
 * ============================================================================
 * Modelo: Secuencial (Línea Base de Referencia)
 * Problema: Trazado de Rayos 3D (Ray Tracing)
 * ============================================================================
 * Ejecuta el algoritmo de trazado de rayos de forma estrictamente lineal
 * sobre un único hilo de CPU, renderizando las filas de la imagen hacia
 * un Framebuffer en RAM contigua desacoplado de Entrada/Salida.
 * ============================================================================
 */

#include "../src/rtweekend.h"
#include "../src/scene.h"
#include "../src/camera.h"
#include "../src/framebuffer.h"

#include <iostream>
#include <chrono>
#include <string>
#include <iomanip>

int main(int argc, char* argv[]) {
    int width = 400;
    int samples = 10;
    int max_depth = 50;
    std::string output_ppm = "";

    // Procesamiento de argumentos CLI
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--width" && i + 1 < argc) {
            width = std::stoi(argv[++i]);
        } else if (arg == "--samples" && i + 1 < argc) {
            samples = std::stoi(argv[++i]);
        } else if (arg == "--depth" && i + 1 < argc) {
            max_depth = std::stoi(argv[++i]);
        } else if (arg == "--save-ppm" && i + 1 < argc) {
            output_ppm = argv[++i];
        }
    }

    // Creación de la escena determinista compartida
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

    // =========================================================================
    // MEDICIÓN DE TIEMPO PURO DE CÓMPUTO (Desacoplado de I/O)
    // =========================================================================
    auto start = std::chrono::high_resolution_clock::now();

    cam.render_slice(world, fb, 0, height);

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> elapsed = end - start;

    std::cout << "========================================================\n";
    std::cout << " Ray Tracing - Secuencial (Línea Base)\n";
    std::cout << "========================================================\n";
    std::cout << "Resolución       : " << width << "x" << height << "\n";
    std::cout << "Muestras / Píxel : " << samples << "\n";
    std::cout << "Profundidad Rebote: " << max_depth << "\n";
    std::cout << "Tiempo de Cómputo: " << std::fixed << std::setprecision(3)
              << elapsed.count() << " ms (" << elapsed.count() / 1000.0 << " s)\n";
    std::cout << "========================================================\n";

    // Salida estructurada CSV por stderr para scripts de benchmarking masivo
    std::cerr << "sequential," << width << "," << height << "," 
              << samples << "," << max_depth << "," << elapsed.count() << "\n";

    // Exportación opcional a disco PPM
    if (!output_ppm.empty()) {
        if (fb.save_ppm(output_ppm)) {
            std::cout << "[INFO] Imagen PPM guardada con éxito en: " << output_ppm << "\n";
        } else {
            std::cerr << "[ERROR] No se pudo guardar la imagen en: " << output_ppm << "\n";
        }
    }

    return 0;
}

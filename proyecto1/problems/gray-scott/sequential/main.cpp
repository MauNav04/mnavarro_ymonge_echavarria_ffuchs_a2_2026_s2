#include "../include/gray_scott_core.hpp"
#include <iostream>
#include <chrono>
#include <string>
#include <filesystem>
#include <iomanip>

namespace fs = std::filesystem;

int main(int argc, char* argv[]) {
    // 1. Parámetros por defecto de la simulación
    int width = 512;
    int height = 512;
    int total_steps = 2000;
    bool save_frames = false;
    int frame_interval = 50; // Guardar 1 fotograma cada 50 pasos
    std::string output_dir = "../video/temp_frames";

    // 2. Parseo simple de argumentos por línea de comandos
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--size" && i + 1 < argc) {
            width = std::stoi(argv[++i]);
            height = width;
        } else if (arg == "--steps" && i + 1 < argc) {
            total_steps = std::stoi(argv[++i]);
        } else if (arg == "--save-frames") {
            save_frames = true;
        } else if (arg == "--output-dir" && i + 1 < argc) {
            output_dir = argv[++i];
        }
    }

    // 3. Inicialización de la memoria y condiciones de frontera (Doble Búfer)
    Grid2D current(width, height);
    Grid2D next(width, height);
    GrayScottParams params;
    init_simulation(current);
    next = current;

    if (save_frames) {
        fs::create_directories(output_dir);
        export_frame_ppm(current, 0, output_dir);
    }

    // 4. Medición estricta de tiempo de cómputo (excluye alocación y parseo)
    int frame_count = 1;
    auto start_time = std::chrono::high_resolution_clock::now();

    for (int step = 1; step <= total_steps; ++step) {
        // Cálculo del stencil secuencial
        step_simulation_sequential(current, next, params);

        // Intercambio de punteros / buffers (Double Buffering)
        std::swap(current, next);

        // Exportación de fotograma únicamente si el flag está activo
        if (save_frames && (step % frame_interval == 0)) {
            export_frame_ppm(current, frame_count++, output_dir);
        }
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> elapsed_ms = end_time - start_time;

    // 5. Salida estandarizada de métricas en consola (fácil de parsear por scripts)
    std::cout << "========================================\n";
    std::cout << " Gray-Scott Baseline (Secuencial)\n";
    std::cout << "========================================\n";
    std::cout << "Dimensiones     : " << width << "x" << height << "\n";
    std::cout << "Pasos totales   : " << total_steps << "\n";
    std::cout << "Tiempo computo  : " << std::fixed << std::setprecision(3) 
              << elapsed_ms.count() << " ms (" << elapsed_ms.count() / 1000.0 << " s)\n";
    std::cout << "Frames guardados: " << (save_frames ? frame_count : 0) << "\n";
    std::cout << "========================================\n";

    // Formato CSV directo en una sola línea para piping/scripts automáticos
    // sequential,width,height,steps,time_ms
    std::cerr << "sequential," << width << "," << height << "," 
              << total_steps << "," << elapsed_ms.count() << "\n";

    return 0;
}
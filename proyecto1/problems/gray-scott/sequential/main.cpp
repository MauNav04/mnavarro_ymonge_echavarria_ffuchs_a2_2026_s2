#include "../include/gray_scott_core.hpp"
#include <iostream>
#include <chrono>
#include <string>
#include <filesystem>
#include <iomanip>

namespace fs = std::filesystem;

int main(int argc, char* argv[]) {
    int width = 512;
    int height = 512;
    int total_steps = 16000;
    bool save_frames = false;
    int frame_interval = 100;
    std::string output_dir = "../videos/temp_frames";

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

    Grid2D current(width, height);
    Grid2D next(width, height);
    GrayScottParams params;

    init_simulation(current);
    next = current;

    if (!save_frames) {
        // =====================================================================
        // RUTA 1: BENCHMARK PURO (Máxima fidelidad, cero overhead de timers)
        // =====================================================================
        auto start = std::chrono::high_resolution_clock::now();

        for (int step = 1; step <= total_steps; ++step) {
            step_simulation_sequential(current, next, params);
            std::swap(current, next);
        }

        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> elapsed = end - start;

        std::cout << "========================================\n";
        std::cout << " Gray-Scott Baseline (Puro Benchmark)\n";
        std::cout << "========================================\n";
        std::cout << "Dimensiones   : " << width << "x" << height << "\n";
        std::cout << "Pasos totales : " << total_steps << "\n";
        std::cout << "Tiempo computo: " << std::fixed << std::setprecision(3) 
                  << elapsed.count() << " ms (" << elapsed.count() / 1000.0 << " s)\n";
        std::cout << "========================================\n";

        // Salida limpia a stderr para scripts automatizados
        std::cerr << "sequential," << width << "," << height << "," 
                  << total_steps << "," << elapsed.count() << "\n";

    } else {
        // =====================================================================
        // RUTA 2: GENERACIÓN DE VIDEO (Exclusiva para material audiovisual)
        // =====================================================================
        fs::create_directories(output_dir);
        export_frame_ppm(current, 0, output_dir);
        int frame_count = 1;

        for (int step = 1; step <= total_steps; ++step) {
            step_simulation_sequential(current, next, params);
            std::swap(current, next);

            if (step % frame_interval == 0) {
                export_frame_ppm(current, frame_count++, output_dir);
            }
        }

        std::cout << "[INFO] Se exportaron " << frame_count << " fotogramas para el video.\n";
    }

    return 0;
}
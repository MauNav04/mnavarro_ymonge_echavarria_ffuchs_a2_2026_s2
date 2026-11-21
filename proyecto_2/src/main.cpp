#include "image_rgba.hpp"
#include "sobel_sequential.hpp"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <string>
#include <vector>

namespace {

struct CliOptions {
    int width = 0;
    int height = 0;
    int runs = 5;
    int warmup = 1;
    std::string save_prefix;
    bool show_help = false;
};

struct Resolution {
    int width;
    int height;
};

void print_usage(const char* prog) {
    std::cout << "Uso: " << prog << " [opciones]\n"
              << "  Sin --width/--height ejecuta la bateria del Avance 1: "
              << "512x512, 1920x1080 y 3840x2160.\n\n"
              << "Opciones:\n"
              << "  --width <W>       Ancho de imagen personalizado\n"
              << "  --height <H>      Alto de imagen personalizado\n"
              << "  --runs <N>        Repeticiones medidas (default: 5)\n"
              << "  --warmup <K>      Corridas de calentamiento (default: 1)\n"
              << "  --save <prefijo>  Guarda <prefijo>_input.png y <prefijo>_output.png\n"
              << "  --help            Muestra esta ayuda\n";
}

bool parse_args(int argc, char** argv, CliOptions& opts) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto need_val = [&](const std::string& flag) -> const char* {
            if (i + 1 >= argc) {
                std::cerr << "Error: falta valor para " << flag << "\n";
                return nullptr;
            }
            return argv[++i];
        };

        if (arg == "--help" || arg == "-h") {
            opts.show_help = true;
            return true;
        } else if (arg == "--width") {
            const char* v = need_val(arg);
            if (!v) return false;
            opts.width = std::stoi(v);
        } else if (arg == "--height") {
            const char* v = need_val(arg);
            if (!v) return false;
            opts.height = std::stoi(v);
        } else if (arg == "--runs") {
            const char* v = need_val(arg);
            if (!v) return false;
            opts.runs = std::stoi(v);
        } else if (arg == "--warmup") {
            const char* v = need_val(arg);
            if (!v) return false;
            opts.warmup = std::stoi(v);
        } else if (arg == "--save") {
            const char* v = need_val(arg);
            if (!v) return false;
            opts.save_prefix = v;
        } else {
            std::cerr << "Argumento desconocido: " << arg << "\n";
            return false;
        }
    }

    if ((opts.width > 0) != (opts.height > 0)) {
        std::cerr << "Error: debes especificar tanto --width como --height.\n";
        return false;
    }
    if (opts.runs <= 0 || opts.warmup < 0) {
        std::cerr << "Error: --runs debe ser > 0 y --warmup >= 0.\n";
        return false;
    }
    return true;
}

uint64_t fnv1a_checksum(const sobel::ImageRGBA& img) {
    uint64_t hash = 14695981039346656037ULL;
    for (const uint8_t b : img.data) {
        hash ^= b;
        hash *= 1099511628211ULL;
    }
    return hash;
}

void run_benchmark(int width, int height, int warmup, int runs, const std::string& save_prefix) {
    const sobel::ImageRGBA input = sobel::make_deterministic_pattern(width, height, 42);
    sobel::ImageRGBA output(width, height);

    for (int w = 0; w < warmup; ++w) {
        sobel::sobel_sequential_into(input, output);
    }

    std::vector<double> times_ms;
    times_ms.reserve(static_cast<std::size_t>(runs));

    for (int r = 0; r < runs; ++r) {
        const auto t0 = std::chrono::steady_clock::now();
        sobel::sobel_sequential_into(input, output);
        const auto t1 = std::chrono::steady_clock::now();
        const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        times_ms.push_back(ms);
    }

    const double sum_ms = std::accumulate(times_ms.begin(), times_ms.end(), 0.0);
    const double mean_ms = sum_ms / static_cast<double>(runs);
    double min_ms = times_ms.front();
    double sq_diff_sum = 0.0;
    for (const double t : times_ms) {
        if (t < min_ms) min_ms = t;
        const double d = t - mean_ms;
        sq_diff_sum += d * d;
    }
    const double std_ms = (runs > 1)
        ? std::sqrt(sq_diff_sum / static_cast<double>(runs - 1))
        : 0.0;

    const double pixels = static_cast<double>(width) * static_cast<double>(height);
    const double mpixels_per_sec = (pixels / (mean_ms / 1000.0)) / 1e6;
    const uint64_t checksum = fnv1a_checksum(output);

    std::cout << std::left << std::setw(12) << (std::to_string(width) + "x" + std::to_string(height))
              << std::right << std::fixed << std::setprecision(3)
              << std::setw(12) << mean_ms
              << std::setw(12) << min_ms
              << std::setw(12) << std_ms
              << std::setw(14) << mpixels_per_sec
              << "   0x" << std::hex << std::setw(16) << std::setfill('0') << checksum
              << std::dec << std::setfill(' ') << "\n";

    if (!save_prefix.empty()) {
        const std::string tag = std::to_string(width) + "x" + std::to_string(height);
        const std::string in_path = save_prefix + "_" + tag + "_input.png";
        const std::string out_path = save_prefix + "_" + tag + "_output.png";
        if (sobel::save_png(in_path, input) && sobel::save_png(out_path, output)) {
            std::cout << "  -> Guardado: " << in_path << " y " << out_path << "\n";
        } else {
            std::cerr << "  -> Error al guardar PNG con prefijo " << save_prefix << "\n";
        }
    }
}

}  // namespace

int main(int argc, char** argv) {
    CliOptions opts;
    if (!parse_args(argc, argv, opts)) {
        print_usage(argv[0]);
        return 1;
    }
    if (opts.show_help) {
        print_usage(argv[0]);
        return 0;
    }

    std::vector<Resolution> targets;
    if (opts.width > 0 && opts.height > 0) {
        targets.push_back({opts.width, opts.height});
    } else {
        targets = {{512, 512}, {1920, 1080}, {3840, 2160}};
    }

    std::cout << "=== Sobel RGBA32 Sequential Baseline ===\n"
              << "Warmup: " << opts.warmup << " | Runs: " << opts.runs << "\n\n";

    std::cout << std::left << std::setw(12) << "Resolucion"
              << std::right
              << std::setw(12) << "Mean (ms)"
              << std::setw(12) << "Min (ms)"
              << std::setw(12) << "Std (ms)"
              << std::setw(14) << "Q (MPix/s)"
              << "   " << std::left << "Checksum (FNV-1a)\n";
    std::cout << std::string(81, '-') << "\n";

    for (const auto& res : targets) {
        run_benchmark(res.width, res.height, opts.warmup, opts.runs, opts.save_prefix);
    }

    return 0;
}

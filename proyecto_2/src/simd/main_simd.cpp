#include "image_rgba.hpp"
#include "sobel_sequential.hpp"
#include "sobel_simd.hpp"

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
    bool skip_seq = false;
    bool show_help = false;
};

struct Resolution {
    int width;
    int height;
};

struct TimingStats {
    double mean_ms = 0.0;
    double min_ms = 0.0;
    double std_ms = 0.0;
    double mpixels_per_sec = 0.0;
};

void print_usage(const char* prog) {
    std::cout << "Uso: " << prog << " [opciones]\n"
              << "  Sin --width/--height ejecuta la bateria: 512x512, 1920x1080 y 3840x2160.\n\n"
              << "Opciones:\n"
              << "  --width <W>       Ancho de imagen personalizado\n"
              << "  --height <H>      Alto de imagen personalizado\n"
              << "  --runs <N>        Repeticiones medidas (default: 5)\n"
              << "  --warmup <K>      Corridas de calentamiento (default: 1)\n"
              << "  --save <prefijo>  Guarda <prefijo>_input.png y <prefijo>_output.png\n"
              << "  --simd-only       Omite la comparacion temporal contra el baseline secuencial\n"
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
        } else if (arg == "--simd-only") {
            opts.skip_seq = true;
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

template <typename Fn>
TimingStats measure_kernel(Fn&& fn, int width, int height, int warmup, int runs) {
    for (int w = 0; w < warmup; ++w) {
        fn();
    }

    std::vector<double> times_ms;
    times_ms.reserve(static_cast<std::size_t>(runs));

    for (int r = 0; r < runs; ++r) {
        const auto t0 = std::chrono::steady_clock::now();
        fn();
        const auto t1 = std::chrono::steady_clock::now();
        times_ms.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
    }

    TimingStats stats;
    const double sum_ms = std::accumulate(times_ms.begin(), times_ms.end(), 0.0);
    stats.mean_ms = sum_ms / static_cast<double>(runs);
    stats.min_ms = times_ms.front();

    double sq_diff_sum = 0.0;
    for (const double t : times_ms) {
        if (t < stats.min_ms) stats.min_ms = t;
        const double d = t - stats.mean_ms;
        sq_diff_sum += d * d;
    }
    stats.std_ms = (runs > 1)
        ? std::sqrt(sq_diff_sum / static_cast<double>(runs - 1))
        : 0.0;

    const double pixels = static_cast<double>(width) * static_cast<double>(height);
    stats.mpixels_per_sec = (pixels / (stats.mean_ms / 1000.0)) / 1e6;
    return stats;
}

bool run_benchmark(int width, int height, int warmup, int runs, bool skip_seq,
                   const std::string& save_prefix) {
    const sobel::ImageRGBA input = sobel::make_deterministic_pattern(width, height, 42);
    sobel::ImageRGBA simd_out(width, height);

    const TimingStats simd_stats = measure_kernel(
        [&]() { sobel::sobel_simd_into(input, simd_out); },
        width, height, warmup, runs);

    const uint64_t simd_hash = fnv1a_checksum(simd_out);

    std::cout << std::left << std::setw(12) << (std::to_string(width) + "x" + std::to_string(height))
              << std::right << std::fixed << std::setprecision(3)
              << std::setw(12) << simd_stats.mean_ms
              << std::setw(12) << simd_stats.min_ms
              << std::setw(12) << simd_stats.std_ms
              << std::setw(14) << simd_stats.mpixels_per_sec;

    bool ok = true;
    if (!skip_seq) {
        sobel::ImageRGBA seq_out(width, height);
        const TimingStats seq_stats = measure_kernel(
            [&]() { sobel::sobel_sequential_into(input, seq_out); },
            width, height, warmup, runs);

        const auto diff = sobel::compare_images(seq_out, simd_out);
        const double speedup = seq_stats.mean_ms / simd_stats.mean_ms;
        ok = diff.exact_match();

        std::cout << std::setw(12) << seq_stats.mean_ms
                  << std::setw(10) << std::setprecision(2) << speedup << "x"
                  << std::setw(10) << (ok ? "EXACT" : "DIFF");
    }

    std::cout << "   0x" << std::hex << std::setw(16) << std::setfill('0') << simd_hash
              << std::dec << std::setfill(' ') << "\n";

    if (!save_prefix.empty()) {
        const std::string tag = std::to_string(width) + "x" + std::to_string(height);
        const std::string in_path = save_prefix + "_" + tag + "_input.png";
        const std::string out_path = save_prefix + "_" + tag + "_simd_output.png";
        if (sobel::save_png(in_path, input) && sobel::save_png(out_path, simd_out)) {
            std::cout << "  -> Guardado: " << in_path << " y " << out_path << "\n";
        } else {
            std::cerr << "  -> Error al guardar PNG con prefijo " << save_prefix << "\n";
        }
    }

    return ok;
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

    std::cout << "=== Sobel RGBA32 SIMD (" << sobel::sobel_simd_backend() << ") ===\n"
              << "Warmup: " << opts.warmup << " | Runs: " << opts.runs << "\n\n";

    std::cout << std::left << std::setw(12) << "Resolucion"
              << std::right
              << std::setw(12) << "SIMD (ms)"
              << std::setw(12) << "Min (ms)"
              << std::setw(12) << "Std (ms)"
              << std::setw(14) << "Q (MPix/s)";
    if (!opts.skip_seq) {
        std::cout << std::setw(12) << "Seq (ms)"
                  << std::setw(11) << "Speedup"
                  << std::setw(10) << "Match";
    }
    std::cout << "   " << std::left << "Checksum (FNV-1a)\n";
    std::cout << std::string(opts.skip_seq ? 81 : 114, '-') << "\n";

    bool all_exact = true;
    for (const auto& res : targets) {
        if (!run_benchmark(res.width, res.height, opts.warmup, opts.runs, opts.skip_seq, opts.save_prefix)) {
            all_exact = false;
        }
    }

    return all_exact ? 0 : 2;
}

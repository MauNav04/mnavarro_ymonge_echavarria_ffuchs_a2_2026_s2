#include "image_rgba.hpp"
#include "sobel_gpu.hpp"
#include "sobel_sequential.hpp"
#include "sobel_simd.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <string>
#include <utility>
#include <vector>

namespace {

enum class VariantSelection {
    Naive,
    Tiled,
    Both,
};

struct CliOptions {
    int device_index = -1;
    int runs = 5;
    int iters = 30;
    int warmup = 3;
    int simd_lanes = 0;  // 0 => autodeteccion segun backend (8 para AVX2, 4 para NEON, 1 para Scalar)
    VariantSelection variant = VariantSelection::Both;
    bool list_devices = false;
    bool run_validate = false;
    bool run_benchmark = false;
    bool show_help = false;
    std::string save_prefix;
    std::string csv_base = "bin/benchmark_results";
};

struct ValidationCase {
    std::string name;
    sobel::ImageRGBA input;
    bool check_zero_interior = false;
    int isolated_channel = -1;  // 0=R, 1=G, 2=B si es caso de canal aislado.
};

struct SampleStats {
    double median = 0.0;
    double stddev = 0.0;
};

struct RawSample {
    std::string resolution;
    int width = 0;
    int height = 0;
    std::string variant;
    int iter = 0;
    double total_ms = 0.0;
    double kernel_ms = 0.0;
    double h2d_ms = 0.0;
    double d2h_ms = 0.0;
};

struct SummaryRow {
    std::string resolution;
    int width = 0;
    int height = 0;
    std::size_t pixels = 0;
    std::string variant;
    int p_units = 1;
    int iters = 0;
    int warmup = 0;
    SampleStats total;
    SampleStats kernel;
    SampleStats h2d;
    SampleStats d2h;
    double speedup_total = 1.0;
    double speedup_kernel = 1.0;
    double throughput_total_mpx_s = 0.0;
    double throughput_kernel_mpx_s = 0.0;
    double efficiency_total = 1.0;
    double efficiency_kernel = 1.0;
    bool exact_match = true;
};

void print_usage(const char* prog) {
    std::cout << "Uso: " << prog << " [opciones]\n\n"
              << "Modos principales:\n"
              << "  --validate                   Bateria de correctitud (Seq vs SIMD, GPU Naive y GPU Tiled)\n"
              << "  --benchmark                  Benchmark completo (512x512, 1920x1080, 3840x2160) + exportacion CSV\n"
              << "  --list-devices               Lista todas las GPUs OpenCL detectadas y sale\n\n"
              << "Opciones de configuracion:\n"
              << "  --device <idx>               Indice de GPU OpenCL (default: auto Intel Iris Xe)\n"
              << "  --iters <N>                  Iteraciones medidas en --benchmark (default: 30)\n"
              << "  --warmup <K>                 Iteraciones de warmup en --benchmark (default: 3)\n"
              << "  --csv <prefijo>              Ruta base para <prefijo>_summary.csv y <prefijo>_raw.csv\n"
              << "                               (default: bin/benchmark_results)\n"
              << "  --simd-lanes <P>             Valor de P para eficiencia SIMD (default: auto, 8 en AVX2)\n"
              << "  --variant <naive|tiled|both> Variante GPU en modo rapido (default: both)\n"
              << "  --runs <N>                   Repeticiones en modo rapido (default: 5)\n"
              << "  --save <prefijo>             Guarda PNGs de entrada/salida para 512x512 en modo rapido\n"
              << "  --help                       Muestra esta ayuda\n";
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
        } else if (arg == "--list-devices") {
            opts.list_devices = true;
        } else if (arg == "--validate") {
            opts.run_validate = true;
        } else if (arg == "--benchmark") {
            opts.run_benchmark = true;
        } else if (arg == "--device") {
            const char* v = need_val(arg);
            if (!v) return false;
            opts.device_index = std::stoi(v);
        } else if (arg == "--iters") {
            const char* v = need_val(arg);
            if (!v) return false;
            opts.iters = std::stoi(v);
        } else if (arg == "--warmup") {
            const char* v = need_val(arg);
            if (!v) return false;
            opts.warmup = std::stoi(v);
        } else if (arg == "--csv") {
            const char* v = need_val(arg);
            if (!v) return false;
            opts.csv_base = v;
        } else if (arg == "--simd-lanes") {
            const char* v = need_val(arg);
            if (!v) return false;
            opts.simd_lanes = std::stoi(v);
        } else if (arg == "--variant") {
            const char* v = need_val(arg);
            if (!v) return false;
            const std::string val(v);
            if (val == "naive") {
                opts.variant = VariantSelection::Naive;
            } else if (val == "tiled") {
                opts.variant = VariantSelection::Tiled;
            } else if (val == "both") {
                opts.variant = VariantSelection::Both;
            } else {
                std::cerr << "Error: valor invalido para --variant: " << val
                          << " (use naive, tiled o both)\n";
                return false;
            }
        } else if (arg == "--runs") {
            const char* v = need_val(arg);
            if (!v) return false;
            opts.runs = std::stoi(v);
        } else if (arg == "--save") {
            const char* v = need_val(arg);
            if (!v) return false;
            opts.save_prefix = v;
        } else {
            std::cerr << "Argumento desconocido: " << arg << "\n";
            return false;
        }
    }

    if (opts.runs <= 0 || opts.iters <= 0 || opts.warmup < 0 || opts.simd_lanes < 0) {
        std::cerr << "Error: --runs y --iters deben ser > 0; --warmup y --simd-lanes deben ser >= 0\n";
        return false;
    }
    return true;
}

void print_device_details(const sobel::GpuDeviceInfo& info) {
    const double local_kb = static_cast<double>(info.local_mem_size_bytes) / 1024.0;
    std::cout << "Dispositivo [" << info.index << "]: " << info.device_name << "\n"
              << "  Plataforma         : " << info.platform_name << "\n"
              << "  Vendor             : " << info.device_vendor << "\n"
              << "  Version OpenCL     : " << info.opencl_version << "\n"
              << "  Compute Units      : " << info.compute_units << "\n"
              << "  Max Work-Group Size: " << info.max_work_group_size << "\n"
              << "  Local Memory Size  : " << info.local_mem_size_bytes << " bytes ("
              << std::fixed << std::setprecision(1) << local_kb << " KB)\n";
}

std::vector<sobel::GpuKernelVariant> resolve_variants(VariantSelection sel) {
    switch (sel) {
        case VariantSelection::Naive:
            return {sobel::GpuKernelVariant::Naive};
        case VariantSelection::Tiled:
            return {sobel::GpuKernelVariant::Tiled18x18};
        case VariantSelection::Both:
            return {sobel::GpuKernelVariant::Naive, sobel::GpuKernelVariant::Tiled18x18};
    }
    return {sobel::GpuKernelVariant::Naive, sobel::GpuKernelVariant::Tiled18x18};
}

int detect_simd_parallelism(int override_lanes) {
    if (override_lanes > 0) {
        return override_lanes;
    }
    const std::string backend = sobel::sobel_simd_backend();
    if (backend.find("AVX2") != std::string::npos) {
        // sobel_simd.cpp procesa 8 pixeles RGBA32 por iteracion en registros AVX2 de 256 bits.
        return 8;
    }
    if (backend.find("NEON") != std::string::npos) {
        return 4;
    }
    return 1;
}

std::string strip_csv_extension(const std::string& path) {
    if (path.size() >= 4 && path.substr(path.size() - 4) == ".csv") {
        return path.substr(0, path.size() - 4);
    }
    return path;
}

SampleStats compute_stats(const std::vector<double>& values) {
    SampleStats s;
    if (values.empty()) {
        return s;
    }

    std::vector<double> sorted = values;
    std::sort(sorted.begin(), sorted.end());
    const std::size_t n = sorted.size();
    if (n % 2U == 1U) {
        s.median = sorted[n / 2U];
    } else {
        s.median = 0.5 * (sorted[n / 2U - 1U] + sorted[n / 2U]);
    }

    const double mean = std::accumulate(values.begin(), values.end(), 0.0) / static_cast<double>(n);
    if (n > 1U) {
        double sq_sum = 0.0;
        for (const double v : values) {
            const double d = v - mean;
            sq_sum += d * d;
        }
        s.stddev = std::sqrt(sq_sum / static_cast<double>(n - 1U));
    }
    return s;
}

double compute_throughput_mpx_s(std::size_t pixels, double time_ms) {
    if (time_ms <= 0.0) {
        return 0.0;
    }
    return static_cast<double>(pixels) / (time_ms * 1000.0);
}

bool verify_zero_interior(const sobel::ImageRGBA& img) {
    for (int y = 1; y < img.height - 1; ++y) {
        for (int x = 1; x < img.width - 1; ++x) {
            if (img.at(x, y, 0) != 0 || img.at(x, y, 1) != 0 ||
                img.at(x, y, 2) != 0 || img.at(x, y, 3) != 255) {
                return false;
            }
        }
    }
    return true;
}

bool verify_isolated_channel(const sobel::ImageRGBA& img, int active_channel) {
    for (int y = 0; y < img.height; ++y) {
        for (int x = 0; x < img.width; ++x) {
            for (int c = 0; c < 3; ++c) {
                if (c != active_channel && img.at(x, y, c) != 0) {
                    return false;
                }
            }
            if (img.at(x, y, 3) != 255) {
                return false;
            }
        }
    }
    return true;
}

std::vector<ValidationCase> build_validation_cases() {
    std::vector<ValidationCase> cases;

    // 1. Imagen constante (interior == 0, bordes segun zero-padding).
    cases.push_back({"constant_32x32", sobel::make_constant_image(32, 32, 100, 150, 200, 128), true, -1});

    // 2. Borde vertical puro y borde horizontal puro (orientacion de Kx y Ky).
    cases.push_back({"vertical_edge_32x32", sobel::make_vertical_edge_image(32, 32), false, -1});
    cases.push_back({"horizontal_edge_32x32", sobel::make_horizontal_edge_image(32, 32), false, -1});

    // 3. Impulso unitario en el centro.
    cases.push_back({"impulse_center_31x31", sobel::make_impulse_image(31, 31, 15, 15, 255, 128, 64), false, -1});

    // 4. Canales aislados (R, G, B) sin mezcla entre canales.
    cases.push_back({"isolated_R_24x24", sobel::make_isolated_channel_image(24, 24, 0, 120), false, 0});
    cases.push_back({"isolated_G_24x24", sobel::make_isolated_channel_image(24, 24, 1, 120), false, 1});
    cases.push_back({"isolated_B_24x24", sobel::make_isolated_channel_image(24, 24, 2, 120), false, 2});

    // 5. Esquinas: (0,0), (W-1,0), (0,H-1), (W-1,H-1) en dimensiones no multiplo de 8 ni 16.
    constexpr int cw = 25;
    constexpr int ch = 19;
    cases.push_back({"corner_tl_25x19", sobel::make_impulse_image(cw, ch, 0, 0, 80, 140, 200), false, -1});
    cases.push_back({"corner_tr_25x19", sobel::make_impulse_image(cw, ch, cw - 1, 0, 80, 140, 200), false, -1});
    cases.push_back({"corner_bl_25x19", sobel::make_impulse_image(cw, ch, 0, ch - 1, 80, 140, 200), false, -1});
    cases.push_back({"corner_br_25x19", sobel::make_impulse_image(cw, ch, cw - 1, ch - 1, 80, 140, 200), false, -1});

    // 6. Dimensiones no multiplos de 16 ni del ancho SIMD (8).
    cases.push_back({"non_mult_18x18", sobel::make_deterministic_pattern(18, 18, 101), false, -1});
    cases.push_back({"non_mult_100x77", sobel::make_deterministic_pattern(100, 77, 202), false, -1});
    cases.push_back({"non_mult_513x513", sobel::make_deterministic_pattern(513, 513, 303), false, -1});

    return cases;
}

/**
 * @brief Bateria completa de validacion de correctitud bit a bit (Fase 3).
 *
 * Compara sobel_sequential contra SIMD, GPU Naive y GPU Tiled 18x18 reportando
 * max_abs_diff y MAE por cada caso y variante.
 */
bool run_validation_suite(sobel::SobelGpuContext& gpu_ctx) {
    const std::string simd_label = std::string("SIMD(") + sobel::sobel_simd_backend() + ")";
    const auto cases = build_validation_cases();

    std::cout << "=== Bateria de Validacion de Correctitud (Fase 3) ===\n";
    print_device_details(gpu_ctx.device_info());
    std::cout << "  Backend SIMD       : " << sobel::sobel_simd_backend() << "\n";
    if (gpu_ctx.build_log().empty()) {
        std::cout << "  Build OpenCL       : OK (sin warnings)\n\n";
    } else {
        std::cout << "  Build OpenCL log   :\n" << gpu_ctx.build_log() << "\n\n";
    }

    std::cout << std::left
              << std::setw(24) << "Caso"
              << std::setw(10) << "Dims"
              << std::setw(22) << "Variante"
              << std::right
              << std::setw(14) << "max_abs_diff"
              << std::setw(14) << "MAE"
              << std::setw(10) << "Estado" << "\n";
    std::cout << std::string(94, '-') << "\n";

    int total_checks = 0;
    int passed_checks = 0;
    bool all_passed = true;

    for (const auto& tc : cases) {
        const auto expected = sobel::sobel_sequential(tc.input);
        const std::string dims = std::to_string(tc.input.width) + "x" + std::to_string(tc.input.height);

        const bool ref_struct_ok =
            (!tc.check_zero_interior || verify_zero_interior(expected)) &&
            (tc.isolated_channel < 0 || verify_isolated_channel(expected, tc.isolated_channel));

        sobel::ImageRGBA simd_out(tc.input.width, tc.input.height);
        sobel::ImageRGBA naive_out(tc.input.width, tc.input.height);
        sobel::ImageRGBA tiled_out(tc.input.width, tc.input.height);

        sobel::sobel_simd_into(tc.input, simd_out);
        gpu_ctx.run(tc.input, naive_out, sobel::GpuKernelVariant::Naive);
        gpu_ctx.run(tc.input, tiled_out, sobel::GpuKernelVariant::Tiled18x18);

        const std::vector<std::pair<std::string, const sobel::ImageRGBA*>> outputs = {
            {simd_label, &simd_out},
            {"gpu_naive", &naive_out},
            {"gpu_tiled", &tiled_out},
        };

        for (const auto& [label, out_ptr] : outputs) {
            ++total_checks;
            const auto diff = sobel::compare_images(expected, *out_ptr);
            const bool struct_ok =
                ref_struct_ok &&
                (!tc.check_zero_interior || verify_zero_interior(*out_ptr)) &&
                (tc.isolated_channel < 0 || verify_isolated_channel(*out_ptr, tc.isolated_channel));
            const bool ok = diff.exact_match() && diff.mae == 0.0 && struct_ok;

            if (ok) {
                ++passed_checks;
            } else {
                all_passed = false;
            }

            std::cout << std::left
                      << std::setw(24) << tc.name
                      << std::setw(10) << dims
                      << std::setw(22) << label
                      << std::right
                      << std::setw(14) << static_cast<int>(diff.max_abs_diff)
                      << std::setw(14) << std::fixed << std::setprecision(6) << diff.mae
                      << std::setw(10) << (ok ? "PASS" : "FAIL") << "\n";
        }
    }

    std::cout << std::string(94, '-') << "\n";
    std::cout << (all_passed ? "[PASS] " : "[FAIL] ")
              << "Validacion completada: " << passed_checks << "/" << total_checks
              << " verificaciones (" << cases.size() << " casos x 3 variantes) con max |O_seq - O_par| == 0.\n";
    return all_passed;
}

bool write_benchmark_csvs(const std::string& csv_base,
                          const std::vector<SummaryRow>& summary_rows,
                          const std::vector<RawSample>& raw_samples) {
    const std::string stem = strip_csv_extension(csv_base);
    const std::string summary_path = stem + "_summary.csv";
    const std::string raw_path = stem + "_raw.csv";

    std::ofstream f_sum(summary_path);
    if (!f_sum) {
        std::cerr << "[ERROR] No se pudo crear el archivo CSV de resumen: " << summary_path << "\n";
        return false;
    }

    f_sum << "resolution,width,height,pixels,variant,p_units,iters,warmup,"
          << "median_total_ms,std_total_ms,median_kernel_ms,std_kernel_ms,"
          << "median_h2d_ms,std_h2d_ms,median_d2h_ms,std_d2h_ms,"
          << "speedup_total,speedup_kernel,throughput_total_mpx_s,throughput_kernel_mpx_s,"
          << "efficiency_total,efficiency_kernel,exact_match\n";
    f_sum << std::fixed << std::setprecision(6);

    for (const auto& r : summary_rows) {
        f_sum << r.resolution << ','
              << r.width << ','
              << r.height << ','
              << r.pixels << ','
              << r.variant << ','
              << r.p_units << ','
              << r.iters << ','
              << r.warmup << ','
              << r.total.median << ','
              << r.total.stddev << ','
              << r.kernel.median << ','
              << r.kernel.stddev << ','
              << r.h2d.median << ','
              << r.h2d.stddev << ','
              << r.d2h.median << ','
              << r.d2h.stddev << ','
              << r.speedup_total << ','
              << r.speedup_kernel << ','
              << r.throughput_total_mpx_s << ','
              << r.throughput_kernel_mpx_s << ','
              << r.efficiency_total << ','
              << r.efficiency_kernel << ','
              << (r.exact_match ? 1 : 0) << '\n';
    }

    std::ofstream f_raw(raw_path);
    if (!f_raw) {
        std::cerr << "[ERROR] No se pudo crear el archivo CSV crudo: " << raw_path << "\n";
        return false;
    }

    f_raw << "resolution,width,height,variant,iter,total_ms,kernel_ms,h2d_ms,d2h_ms\n";
    f_raw << std::fixed << std::setprecision(6);

    for (const auto& s : raw_samples) {
        f_raw << s.resolution << ','
              << s.width << ','
              << s.height << ','
              << s.variant << ','
              << s.iter << ','
              << s.total_ms << ','
              << s.kernel_ms << ','
              << s.h2d_ms << ','
              << s.d2h_ms << '\n';
    }

    std::cout << "\nCSV exportados correctamente:\n"
              << "  -> Resumen : " << summary_path << "\n"
              << "  -> Crudos  : " << raw_path << "\n";
    return true;
}

/**
 * @brief Harness de benchmarking multi-resolucion (Fase 4).
 *
 * Evalua Seq, SIMD, GPU Naive y GPU Tiled en 512x512, 1920x1080 y 3840x2160,
 * separando T_h2d, T_kernel, T_d2h (eventos OpenCL) y T_total (steady_clock),
 * calculando mediana, desviacion estandar, speedup, throughput y eficiencia paralela.
 */
bool run_benchmark_suite(sobel::SobelGpuContext& gpu_ctx, const CliOptions& opts) {
    const int p_seq = 1;
    const int p_simd = detect_simd_parallelism(opts.simd_lanes);
    const int p_gpu = gpu_ctx.device_info().compute_units > 0
                          ? static_cast<int>(gpu_ctx.device_info().compute_units)
                          : 1;

    std::cout << "=== Harness de Benchmarking DLP - Sobel RGBA32 (Fase 4) ===\n";
    print_device_details(gpu_ctx.device_info());
    std::cout << "  Backend SIMD       : " << sobel::sobel_simd_backend() << "\n"
              << "  Build OpenCL       : "
              << (gpu_ctx.build_log().empty() ? "OK (sin warnings)" : gpu_ctx.build_log()) << "\n"
              << "  Protocolo          : warmup=" << opts.warmup << ", iters=" << opts.iters << "\n"
              << "  Paralelismo (P)    : Seq P=" << p_seq
              << " | SIMD P=" << p_simd << " (carriles por vector)"
              << " | GPU P=" << p_gpu << " (Execution Units)\n\n";

    const std::vector<std::pair<int, int>> resolutions = {
        {512, 512},
        {1920, 1080},
        {3840, 2160},
    };

    std::vector<SummaryRow> summary_rows;
    std::vector<RawSample> raw_samples;
    bool all_exact = true;

    std::cout << std::left
              << std::setw(11) << "Resolucion"
              << std::setw(11) << "Variante"
              << std::right
              << std::setw(5) << "P"
              << std::setw(11) << "MedTot(ms)"
              << std::setw(10) << "StdTot"
              << std::setw(11) << "MedKrn(ms)"
              << std::setw(9) << "H2D(ms)"
              << std::setw(9) << "D2H(ms)"
              << std::setw(9) << "S_tot"
              << std::setw(9) << "S_krn"
              << std::setw(11) << "Q_krn(MP/s)"
              << std::setw(9) << "E_krn"
              << std::setw(7) << "Match" << "\n";
    std::cout << std::string(122, '-') << "\n";

    for (const auto& res : resolutions) {
        const int w = res.first;
        const int h = res.second;
        const std::string res_label = std::to_string(w) + "x" + std::to_string(h);
        const std::size_t pixels = static_cast<std::size_t>(w) * static_cast<std::size_t>(h);
        const sobel::ImageRGBA input = sobel::make_deterministic_pattern(w, h, 42);

        sobel::ImageRGBA seq_out(w, h);
        sobel::ImageRGBA simd_out(w, h);
        sobel::ImageRGBA naive_out(w, h);
        sobel::ImageRGBA tiled_out(w, h);

        auto measure_cpu = [&](const std::string& variant_name, int p_units,
                               auto&& fn, sobel::ImageRGBA& out,
                               const sobel::ImageRGBA* ref_out) -> SummaryRow {
            for (int k = 0; k < opts.warmup; ++k) {
                fn();
            }

            std::vector<double> totals;
            totals.reserve(static_cast<std::size_t>(opts.iters));
            for (int it = 0; it < opts.iters; ++it) {
                const auto t0 = std::chrono::steady_clock::now();
                fn();
                const auto t1 = std::chrono::steady_clock::now();
                const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
                totals.push_back(ms);
                raw_samples.push_back({res_label, w, h, variant_name, it + 1, ms, ms, 0.0, 0.0});
            }

            SummaryRow row;
            row.resolution = res_label;
            row.width = w;
            row.height = h;
            row.pixels = pixels;
            row.variant = variant_name;
            row.p_units = p_units;
            row.iters = opts.iters;
            row.warmup = opts.warmup;
            row.total = compute_stats(totals);
            row.kernel = row.total;
            row.exact_match = (ref_out == nullptr) ? true : sobel::compare_images(*ref_out, out).exact_match();
            return row;
        };

        auto measure_gpu = [&](const std::string& variant_name, sobel::GpuKernelVariant variant,
                               sobel::ImageRGBA& out, const sobel::ImageRGBA& ref_out) -> SummaryRow {
            gpu_ctx.ensure_buffers(w, h);

            for (int k = 0; k < opts.warmup; ++k) {
                gpu_ctx.run(input, out, variant, nullptr);
            }

            std::vector<double> totals, kernels, h2ds, d2hs;
            totals.reserve(static_cast<std::size_t>(opts.iters));
            kernels.reserve(static_cast<std::size_t>(opts.iters));
            h2ds.reserve(static_cast<std::size_t>(opts.iters));
            d2hs.reserve(static_cast<std::size_t>(opts.iters));

            for (int it = 0; it < opts.iters; ++it) {
                sobel::GpuTimingResult tm;
                gpu_ctx.run(input, out, variant, &tm);
                totals.push_back(tm.total_ms);
                kernels.push_back(tm.kernel_ms);
                h2ds.push_back(tm.h2d_ms);
                d2hs.push_back(tm.d2h_ms);
                raw_samples.push_back({res_label, w, h, variant_name, it + 1,
                                       tm.total_ms, tm.kernel_ms, tm.h2d_ms, tm.d2h_ms});
            }

            SummaryRow row;
            row.resolution = res_label;
            row.width = w;
            row.height = h;
            row.pixels = pixels;
            row.variant = variant_name;
            row.p_units = p_gpu;
            row.iters = opts.iters;
            row.warmup = opts.warmup;
            row.total = compute_stats(totals);
            row.kernel = compute_stats(kernels);
            row.h2d = compute_stats(h2ds);
            row.d2h = compute_stats(d2hs);
            row.exact_match = sobel::compare_images(ref_out, out).exact_match();
            return row;
        };

        SummaryRow seq_row = measure_cpu("seq", p_seq,
                                         [&]() { sobel::sobel_sequential_into(input, seq_out); },
                                         seq_out, nullptr);
        SummaryRow simd_row = measure_cpu("simd", p_simd,
                                          [&]() { sobel::sobel_simd_into(input, simd_out); },
                                          simd_out, &seq_out);
        SummaryRow naive_row = measure_gpu("gpu_naive", sobel::GpuKernelVariant::Naive, naive_out, seq_out);
        SummaryRow tiled_row = measure_gpu("gpu_tiled", sobel::GpuKernelVariant::Tiled18x18, tiled_out, seq_out);

        const double seq_med_ms = seq_row.total.median;
        std::vector<SummaryRow> res_rows = {seq_row, simd_row, naive_row, tiled_row};

        for (auto& r : res_rows) {
            r.speedup_total = (r.total.median > 0.0) ? (seq_med_ms / r.total.median) : 0.0;
            r.speedup_kernel = (r.kernel.median > 0.0) ? (seq_med_ms / r.kernel.median) : 0.0;
            r.throughput_total_mpx_s = compute_throughput_mpx_s(pixels, r.total.median);
            r.throughput_kernel_mpx_s = compute_throughput_mpx_s(pixels, r.kernel.median);
            r.efficiency_total = r.speedup_total / static_cast<double>(r.p_units);
            r.efficiency_kernel = r.speedup_kernel / static_cast<double>(r.p_units);

            if (!r.exact_match) {
                all_exact = false;
            }

            std::cout << std::left
                      << std::setw(11) << r.resolution
                      << std::setw(11) << r.variant
                      << std::right << std::fixed
                      << std::setw(5) << r.p_units
                      << std::setw(11) << std::setprecision(3) << r.total.median
                      << std::setw(10) << std::setprecision(3) << r.total.stddev
                      << std::setw(11) << std::setprecision(3) << r.kernel.median
                      << std::setw(9) << std::setprecision(3) << r.h2d.median
                      << std::setw(9) << std::setprecision(3) << r.d2h.median
                      << std::setw(8) << std::setprecision(2) << r.speedup_total << "x"
                      << std::setw(8) << std::setprecision(2) << r.speedup_kernel << "x"
                      << std::setw(11) << std::setprecision(1) << r.throughput_kernel_mpx_s
                      << std::setw(9) << std::setprecision(3) << r.efficiency_kernel
                      << std::setw(7) << (r.exact_match ? "EXACT" : "FAIL") << "\n";

            summary_rows.push_back(r);
        }
        std::cout << std::string(122, '-') << "\n";
    }

    const bool csv_ok = write_benchmark_csvs(opts.csv_base, summary_rows, raw_samples);
    if (!all_exact) {
        std::cerr << "[FAIL] Al menos una variante fallo la comprobacion bit a bit durante --benchmark.\n";
    }
    return all_exact && csv_ok;
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

    try {
        if (opts.list_devices) {
            const auto devices = sobel::SobelGpuContext::list_gpu_devices();
            if (devices.empty()) {
                std::cout << "No se detectaron dispositivos GPU OpenCL.\n";
                return 1;
            }
            std::cout << "=== Dispositivos GPU OpenCL Disponibles (" << devices.size() << ") ===\n";
            for (const auto& dev : devices) {
                print_device_details(dev);
                std::cout << "\n";
            }
            return 0;
        }

        // Un solo contexto OpenCL persistente: compila sobel_rgba_naive y sobel_rgba_tiled una sola vez.
        sobel::SobelGpuContext gpu_ctx(opts.device_index);

        if (opts.run_validate) {
            return run_validation_suite(gpu_ctx) ? 0 : 1;
        }

        if (opts.run_benchmark) {
            return run_benchmark_suite(gpu_ctx, opts) ? 0 : 1;
        }

        std::cout << "=== Sobel RGBA32 GPU (OpenCL - Naive uchar4 vs Tiled 18x18) ===\n";
        print_device_details(gpu_ctx.device_info());
        if (gpu_ctx.build_log().empty()) {
            std::cout << "  Build OpenCL       : OK (sin warnings)\n";
        } else {
            std::cout << "  Build OpenCL log   :\n" << gpu_ctx.build_log() << "\n";
        }
        std::cout << "Ejecuciones consecutivas por resolucion y variante: " << opts.runs << "\n\n";

        const auto variants = resolve_variants(opts.variant);
        const std::vector<std::pair<int, int>> resolutions = {
            {18, 18},
            {64, 64},
            {512, 512},
            {513, 513},
        };

        for (const auto [w, h] : resolutions) {
            const auto input = sobel::make_deterministic_pattern(w, h, 42);
            const auto expected = sobel::sobel_sequential(input);
            sobel::ImageRGBA actual(w, h);

            gpu_ctx.ensure_buffers(w, h);

            for (const sobel::GpuKernelVariant variant : variants) {
                double sum_h2d = 0.0;
                double sum_kernel = 0.0;
                double sum_d2h = 0.0;
                double sum_total = 0.0;

                for (int r = 0; r < opts.runs; ++r) {
                    sobel::GpuTimingResult timing;
                    gpu_ctx.run(input, actual, variant, &timing);

                    const auto diff = sobel::compare_images(expected, actual);
                    if (!diff.exact_match()) {
                        std::cerr << "[FAIL] " << sobel::gpu_kernel_variant_name(variant)
                                  << " [" << w << "x" << h << "] (run " << (r + 1) << ")"
                                  << " | max_diff=" << static_cast<int>(diff.max_abs_diff)
                                  << " | mae=" << diff.mae << "\n";
                        return 1;
                    }

                    sum_h2d += timing.h2d_ms;
                    sum_kernel += timing.kernel_ms;
                    sum_d2h += timing.d2h_ms;
                    sum_total += timing.total_ms;
                }

                const double denom = static_cast<double>(opts.runs);
                std::cout << "[" << std::setw(7) << (std::to_string(w) + "x" + std::to_string(h)) << "] "
                          << std::left << std::setw(10) << sobel::gpu_kernel_variant_name(variant) << std::right
                          << " | avg H2D=" << std::fixed << std::setprecision(3) << (sum_h2d / denom) << " ms"
                          << " | Kernel=" << (sum_kernel / denom) << " ms"
                          << " | D2H=" << (sum_d2h / denom) << " ms"
                          << " | Total=" << (sum_total / denom) << " ms"
                          << " | exact_match=PASS (" << opts.runs << "/" << opts.runs << ")\n";

                if (!opts.save_prefix.empty() && w == 512 && h == 512) {
                    const std::string in_path = opts.save_prefix + "_512x512_input.png";
                    const std::string out_path = opts.save_prefix + "_512x512_" +
                                                 sobel::gpu_kernel_variant_name(variant) + "_output.png";
                    if (sobel::save_png(in_path, input) && sobel::save_png(out_path, actual)) {
                        std::cout << "  -> Guardado: " << in_path << " y " << out_path << "\n";
                    } else {
                        std::cerr << "  -> Error al guardar PNG con prefijo " << opts.save_prefix << "\n";
                    }
                }
            }
        }
    } catch (const std::exception& ex) {
        std::cerr << "[ERROR] " << ex.what() << "\n";
        return 1;
    }
    return 0;
}

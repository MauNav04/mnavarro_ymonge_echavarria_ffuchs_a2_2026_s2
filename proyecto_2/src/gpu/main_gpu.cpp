#include "image_rgba.hpp"
#include "sobel_gpu.hpp"
#include "sobel_sequential.hpp"
#include "sobel_simd.hpp"

#include <iomanip>
#include <iostream>
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
    VariantSelection variant = VariantSelection::Both;
    bool list_devices = false;
    bool run_validate = false;
    bool show_help = false;
    std::string save_prefix;
};

struct ValidationCase {
    std::string name;
    sobel::ImageRGBA input;
    bool check_zero_interior = false;
    int isolated_channel = -1;  // 0=R, 1=G, 2=B si es caso de canal aislado.
};

void print_usage(const char* prog) {
    std::cout << "Uso: " << prog << " [opciones]\n\n"
              << "Opciones (Fase 3 - Kernels GPU + Bateria de Validacion Unificada):\n"
              << "  --validate                  Ejecuta la bateria de correctitud (Seq vs SIMD, GPU Naive y GPU Tiled)\n"
              << "  --list-devices              Lista todas las GPUs OpenCL detectadas y sale\n"
              << "  --device <idx>              Selecciona el indice de GPU OpenCL (default: auto Intel Iris Xe)\n"
              << "  --variant <naive|tiled|both> Variante de kernel GPU a ejecutar (default: both)\n"
              << "  --runs <N>                  Repeticiones consecutivas sin recompilar (default: 5)\n"
              << "  --save <prefijo>            Guarda PNGs de entrada/salida para 512x512\n"
              << "  --help                      Muestra esta ayuda\n";
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
        } else if (arg == "--device") {
            const char* v = need_val(arg);
            if (!v) return false;
            opts.device_index = std::stoi(v);
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

    if (opts.runs <= 0) {
        std::cerr << "Error: --runs debe ser > 0\n";
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
              << std::setw(14) << "Variante"
              << std::right
              << std::setw(14) << "max_abs_diff"
              << std::setw(14) << "MAE"
              << std::setw(10) << "Estado" << "\n";
    std::cout << std::string(86, '-') << "\n";

    int total_checks = 0;
    int passed_checks = 0;
    bool all_passed = true;

    for (const auto& tc : cases) {
        const auto expected = sobel::sobel_sequential(tc.input);
        const std::string dims = std::to_string(tc.input.width) + "x" + std::to_string(tc.input.height);

        // Validar las propiedades estructurales sobre la referencia secuencial tambien.
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
                      << std::setw(14) << label
                      << std::right
                      << std::setw(14) << static_cast<int>(diff.max_abs_diff)
                      << std::setw(14) << std::fixed << std::setprecision(6) << diff.mae
                      << std::setw(10) << (ok ? "PASS" : "FAIL") << "\n";
        }
    }

    std::cout << std::string(86, '-') << "\n";
    std::cout << (all_passed ? "[PASS] " : "[FAIL] ")
              << "Validacion completada: " << passed_checks << "/" << total_checks
              << " verificaciones (" << cases.size() << " casos x 3 variantes) con max |O_seq - O_par| == 0.\n";
    return all_passed;
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

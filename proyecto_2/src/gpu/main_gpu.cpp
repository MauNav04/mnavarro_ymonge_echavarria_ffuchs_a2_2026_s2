#include "image_rgba.hpp"
#include "sobel_gpu.hpp"
#include "sobel_sequential.hpp"

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
    bool show_help = false;
    std::string save_prefix;
};

void print_usage(const char* prog) {
    std::cout << "Uso: " << prog << " [opciones]\n\n"
              << "Opciones (Fase 2 - Kernels uchar4 Naive y Tiled 18x18 en memoria local):\n"
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

        std::cout << "=== Sobel RGBA32 GPU (OpenCL - Fase 2: Naive uchar4 vs Tiled 18x18) ===\n";
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

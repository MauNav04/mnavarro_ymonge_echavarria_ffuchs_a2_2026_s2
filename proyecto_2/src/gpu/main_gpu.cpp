#include "image_rgba.hpp"
#include "opencl_loader.hpp"
#include "sobel_gpu.hpp"
#include "sobel_sequential.hpp"

#include <chrono>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace {

void print_gpu_probe() {
    sobel::gpu::OpenCLApi cl;
    std::string err;
    if (!cl.load(err)) {
        std::cerr << "[ERROR] " << err << "\n";
        return;
    }

    sobel::gpu::cl_uint num_platforms = 0;
    if (cl.clGetPlatformIDs(0, nullptr, &num_platforms) != sobel::gpu::CL_SUCCESS || num_platforms == 0) {
        cl.unload();
        return;
    }

    std::vector<sobel::gpu::cl_platform_id> platforms(num_platforms);
    cl.clGetPlatformIDs(num_platforms, platforms.data(), nullptr);

    std::cout << "=== GPU Dummy (OpenCL - Hardware Real) ===\n";
    for (sobel::gpu::cl_uint p = 0; p < num_platforms; ++p) {
        sobel::gpu::cl_uint num_gpus = 0;
        if (cl.clGetDeviceIDs(platforms[p], sobel::gpu::CL_DEVICE_TYPE_GPU, 0, nullptr, &num_gpus) !=
                sobel::gpu::CL_SUCCESS ||
            num_gpus == 0) {
            continue;
        }
        std::vector<sobel::gpu::cl_device_id> devices(num_gpus);
        cl.clGetDeviceIDs(platforms[p], sobel::gpu::CL_DEVICE_TYPE_GPU, num_gpus, devices.data(), nullptr);

        const std::string dname = sobel::gpu::get_device_string(cl, devices[0], sobel::gpu::CL_DEVICE_NAME);
        const std::string dver = sobel::gpu::get_device_string(cl, devices[0], sobel::gpu::CL_DEVICE_VERSION);
        sobel::gpu::cl_uint cu = 0;
        cl.clGetDeviceInfo(devices[0], sobel::gpu::CL_DEVICE_MAX_COMPUTE_UNITS, sizeof(cu), &cu, nullptr);
        std::cout << "Dispositivo: " << dname << " | " << dver << " | CUs: " << cu << "\n\n";
        break;
    }
    cl.unload();
}

}  // namespace

int main(int argc, char** argv) {
    std::string save_prefix;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--save" && i + 1 < argc) {
            save_prefix = argv[++i];
        } else if (arg == "--save") {
            std::cerr << "Error: falta valor para --save\n";
            return 1;
        }
    }

    try {
        print_gpu_probe();

        // Ejecucion dummy/preliminar sobre una resolucion parcial (64x64) y 512x512.
        for (const auto [w, h] : {std::pair<int, int>{64, 64}, std::pair<int, int>{512, 512}}) {
            const auto input = sobel::make_deterministic_pattern(w, h, 42);
            const auto expected = sobel::sobel_sequential(input);

            sobel::ImageRGBA actual(w, h);
            const auto t0 = std::chrono::steady_clock::now();
            sobel::sobel_gpu_into(input, actual);
            const auto t1 = std::chrono::steady_clock::now();
            const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

            const auto diff = sobel::compare_images(expected, actual);
            std::cout << "[" << w << "x" << h << "] "
                      << "Tiempo total (setup+copia+kernel): " << std::fixed << std::setprecision(3) << ms << " ms | "
                      << "max_diff=" << static_cast<int>(diff.max_abs_diff) << " | "
                      << "exact_match=" << (diff.exact_match() ? "PASS" : "FAIL") << "\n";

            if (!diff.exact_match()) {
                return 1;
            }

            if (!save_prefix.empty() && w == 512 && h == 512) {
                const std::string in_path = save_prefix + "_512x512_input.png";
                const std::string out_path = save_prefix + "_512x512_output.png";
                if (sobel::save_png(in_path, input) && sobel::save_png(out_path, actual)) {
                    std::cout << "  -> Guardado: " << in_path << " y " << out_path << "\n";
                } else {
                    std::cerr << "  -> Error al guardar PNG con prefijo " << save_prefix << "\n";
                }
            }
        }
    } catch (const std::exception& ex) {
        std::cerr << "[ERROR] " << ex.what() << "\n";
        return 1;
    }
    return 0;
}

#include "opencl_loader.hpp"

#include <iostream>
#include <string>
#include <vector>

/**
 * @brief Paso 1 (GPU Dummy): Sonda de plataforma y dispositivo GPU mediante OpenCL.
 */
int main() {
    sobel::gpu::OpenCLApi cl;
    std::string err;
    if (!cl.load(err)) {
        std::cerr << "[ERROR] " << err << "\n";
        return 1;
    }

    sobel::gpu::cl_uint num_platforms = 0;
    if (cl.clGetPlatformIDs(0, nullptr, &num_platforms) != sobel::gpu::CL_SUCCESS || num_platforms == 0) {
        std::cerr << "[ERROR] No se encontraron plataformas OpenCL.\n";
        cl.unload();
        return 1;
    }

    std::vector<sobel::gpu::cl_platform_id> platforms(num_platforms);
    cl.clGetPlatformIDs(num_platforms, platforms.data(), nullptr);

    std::cout << "=== Sonda OpenCL (Hardware GPU Real) ===\n";
    std::cout << "Plataformas detectadas: " << num_platforms << "\n\n";

    bool found_gpu = false;
    for (sobel::gpu::cl_uint p = 0; p < num_platforms; ++p) {
        const std::string pname = sobel::gpu::get_platform_string(cl, platforms[p], sobel::gpu::CL_PLATFORM_NAME);
        const std::string pvendor = sobel::gpu::get_platform_string(cl, platforms[p], sobel::gpu::CL_PLATFORM_VENDOR);
        std::cout << "Plataforma [" << p << "]: " << pname << " (" << pvendor << ")\n";

        sobel::gpu::cl_uint num_gpus = 0;
        const sobel::gpu::cl_int rc =
            cl.clGetDeviceIDs(platforms[p], sobel::gpu::CL_DEVICE_TYPE_GPU, 0, nullptr, &num_gpus);
        if (rc != sobel::gpu::CL_SUCCESS || num_gpus == 0) {
            std::cout << "  (Sin dispositivos GPU en esta plataforma)\n";
            continue;
        }

        std::vector<sobel::gpu::cl_device_id> devices(num_gpus);
        cl.clGetDeviceIDs(platforms[p], sobel::gpu::CL_DEVICE_TYPE_GPU, num_gpus, devices.data(), nullptr);

        for (sobel::gpu::cl_uint d = 0; d < num_gpus; ++d) {
            found_gpu = true;
            const std::string dname = sobel::gpu::get_device_string(cl, devices[d], sobel::gpu::CL_DEVICE_NAME);
            const std::string dver = sobel::gpu::get_device_string(cl, devices[d], sobel::gpu::CL_DEVICE_VERSION);

            sobel::gpu::cl_uint compute_units = 0;
            std::size_t max_wg = 0;
            sobel::gpu::cl_ulong local_mem = 0;
            cl.clGetDeviceInfo(devices[d], sobel::gpu::CL_DEVICE_MAX_COMPUTE_UNITS, sizeof(compute_units), &compute_units, nullptr);
            cl.clGetDeviceInfo(devices[d], sobel::gpu::CL_DEVICE_MAX_WORK_GROUP_SIZE, sizeof(max_wg), &max_wg, nullptr);
            cl.clGetDeviceInfo(devices[d], sobel::gpu::CL_DEVICE_LOCAL_MEM_SIZE, sizeof(local_mem), &local_mem, nullptr);

            std::cout << "  -> GPU [" << d << "]: " << dname << "\n"
                      << "     OpenCL:        " << dver << "\n"
                      << "     Compute Units: " << compute_units << "\n"
                      << "     Max WorkGroup: " << max_wg << "\n"
                      << "     Local Memory:  " << (local_mem / 1024) << " KB\n";
        }
    }

    cl.unload();
    return found_gpu ? 0 : 1;
}

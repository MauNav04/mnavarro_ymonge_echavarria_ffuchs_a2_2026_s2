#include "sobel_gpu.hpp"

#include "opencl_loader.hpp"

#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>

namespace sobel {

namespace {

// Kernel OpenCL preliminar (Propuesta 1 del Avance 1: memoria global, 1 hilo por píxel).
constexpr const char* kSobelNaiveKernelSource = R"CLC(
__kernel void sobel_rgba_naive(
    __global const uchar* input,
    __global uchar* output,
    const int width,
    const int height)
{
    const int x = (int)get_global_id(0);
    const int y = (int)get_global_id(1);
    if (x >= width || y >= height) {
        return;
    }

    const int kx[3][3] = {
        {-1, 0, 1},
        {-2, 0, 2},
        {-1, 0, 1}
    };
    const int ky[3][3] = {
        {-1, -2, -1},
        { 0,  0,  0},
        { 1,  2,  1}
    };

    int gx0 = 0, gx1 = 0, gx2 = 0;
    int gy0 = 0, gy1 = 0, gy2 = 0;

    for (int i = -1; i <= 1; ++i) {
        const int sy = y + i;
        if (sy < 0 || sy >= height) {
            continue;
        }
        const int kr = i + 1;

        for (int j = -1; j <= 1; ++j) {
            const int sx = x + j;
            if (sx < 0 || sx >= width) {
                continue;
            }
            const int kc = j + 1;
            const int wx = kx[kr][kc];
            const int wy = ky[kr][kc];

            const int off = (sy * width + sx) * 4;
            const int r = (int)input[off + 0];
            const int g = (int)input[off + 1];
            const int b = (int)input[off + 2];

            gx0 += r * wx; gy0 += r * wy;
            gx1 += g * wx; gy1 += g * wy;
            gx2 += b * wx; gy2 += b * wy;
        }
    }

    const int m0 = abs(gx0) + abs(gy0);
    const int m1 = abs(gx1) + abs(gy1);
    const int m2 = abs(gx2) + abs(gy2);

    const int dst = (y * width + x) * 4;
    output[dst + 0] = (uchar)(m0 > 255 ? 255 : m0);
    output[dst + 1] = (uchar)(m1 > 255 ? 255 : m1);
    output[dst + 2] = (uchar)(m2 > 255 ? 255 : m2);
    output[dst + 3] = (uchar)255;
}
)CLC";

std::size_t round_up(std::size_t value, std::size_t multiple) {
    return ((value + multiple - 1U) / multiple) * multiple;
}

gpu::cl_device_id find_first_gpu(const gpu::OpenCLApi& cl) {
    gpu::cl_uint num_platforms = 0;
    if (cl.clGetPlatformIDs(0, nullptr, &num_platforms) != gpu::CL_SUCCESS || num_platforms == 0) {
        return nullptr;
    }
    std::vector<gpu::cl_platform_id> platforms(num_platforms);
    cl.clGetPlatformIDs(num_platforms, platforms.data(), nullptr);

    for (gpu::cl_platform_id plat : platforms) {
        gpu::cl_uint num_gpus = 0;
        if (cl.clGetDeviceIDs(plat, gpu::CL_DEVICE_TYPE_GPU, 0, nullptr, &num_gpus) == gpu::CL_SUCCESS &&
            num_gpus > 0) {
            std::vector<gpu::cl_device_id> devices(num_gpus);
            if (cl.clGetDeviceIDs(plat, gpu::CL_DEVICE_TYPE_GPU, num_gpus, devices.data(), nullptr) == gpu::CL_SUCCESS) {
                return devices.front();
            }
        }
    }
    return nullptr;
}

struct OpenCLContextGuard {
    const gpu::OpenCLApi& cl;
    gpu::cl_context ctx = nullptr;
    gpu::cl_command_queue queue = nullptr;
    gpu::cl_program program = nullptr;
    gpu::cl_kernel kernel = nullptr;
    gpu::cl_mem buf_in = nullptr;
    gpu::cl_mem buf_out = nullptr;

    ~OpenCLContextGuard() {
        if (buf_out) cl.clReleaseMemObject(buf_out);
        if (buf_in) cl.clReleaseMemObject(buf_in);
        if (kernel) cl.clReleaseKernel(kernel);
        if (program) cl.clReleaseProgram(program);
        if (queue) cl.clReleaseCommandQueue(queue);
        if (ctx) cl.clReleaseContext(ctx);
    }
};

}  // namespace

void sobel_gpu_into(const ImageRGBA& input, ImageRGBA& output) {
    if (!input.is_valid()) {
        throw std::invalid_argument("Input image is not valid");
    }
    if (output.width != input.width || output.height != input.height || !output.is_valid()) {
        throw std::invalid_argument("Output image dimensions must match input image");
    }

    gpu::OpenCLApi cl;
    std::string err;
    if (!cl.load(err)) {
        throw std::runtime_error(err);
    }

    struct ApiUnloader {
        gpu::OpenCLApi& api;
        ~ApiUnloader() { api.unload(); }
    } unloader{cl};

    gpu::cl_device_id device = find_first_gpu(cl);
    if (!device) {
        throw std::runtime_error("No se encontro dispositivo GPU OpenCL");
    }

    OpenCLContextGuard g{cl};
    gpu::cl_int rc = gpu::CL_SUCCESS;

    g.ctx = cl.clCreateContext(nullptr, 1, &device, nullptr, nullptr, &rc);
    if (!g.ctx || rc != gpu::CL_SUCCESS) {
        throw std::runtime_error("Error al crear contexto OpenCL");
    }

    g.queue = cl.clCreateCommandQueue(g.ctx, device, 0, &rc);
    if (!g.queue || rc != gpu::CL_SUCCESS) {
        throw std::runtime_error("Error al crear command queue OpenCL");
    }

    const char* src_ptr = kSobelNaiveKernelSource;
    g.program = cl.clCreateProgramWithSource(g.ctx, 1, &src_ptr, nullptr, &rc);
    if (!g.program || rc != gpu::CL_SUCCESS) {
        throw std::runtime_error("Error al crear programa OpenCL");
    }

    rc = cl.clBuildProgram(g.program, 1, &device, nullptr, nullptr, nullptr);
    if (rc != gpu::CL_SUCCESS) {
        std::size_t log_size = 0;
        cl.clGetProgramBuildInfo(g.program, device, gpu::CL_PROGRAM_BUILD_LOG, 0, nullptr, &log_size);
        std::string log(log_size, '\0');
        if (log_size > 0) {
            cl.clGetProgramBuildInfo(g.program, device, gpu::CL_PROGRAM_BUILD_LOG, log_size, log.data(), nullptr);
        }
        throw std::runtime_error("Error compilando kernel OpenCL:\n" + log);
    }

    g.kernel = cl.clCreateKernel(g.program, "sobel_rgba_naive", &rc);
    if (!g.kernel || rc != gpu::CL_SUCCESS) {
        throw std::runtime_error("Error al crear kernel sobel_rgba_naive");
    }

    const std::size_t bytes = input.byte_size();
    g.buf_in = cl.clCreateBuffer(g.ctx, gpu::CL_MEM_READ_ONLY | gpu::CL_MEM_COPY_HOST_PTR,
                                 bytes, const_cast<uint8_t*>(input.data.data()), &rc);
    if (!g.buf_in || rc != gpu::CL_SUCCESS) {
        throw std::runtime_error("Error al crear buffer de entrada en GPU");
    }

    g.buf_out = cl.clCreateBuffer(g.ctx, gpu::CL_MEM_WRITE_ONLY, bytes, nullptr, &rc);
    if (!g.buf_out || rc != gpu::CL_SUCCESS) {
        throw std::runtime_error("Error al crear buffer de salida en GPU");
    }

    const int width = input.width;
    const int height = input.height;
    rc = cl.clSetKernelArg(g.kernel, 0, sizeof(gpu::cl_mem), &g.buf_in);
    rc |= cl.clSetKernelArg(g.kernel, 1, sizeof(gpu::cl_mem), &g.buf_out);
    rc |= cl.clSetKernelArg(g.kernel, 2, sizeof(int), &width);
    rc |= cl.clSetKernelArg(g.kernel, 3, sizeof(int), &height);
    if (rc != gpu::CL_SUCCESS) {
        throw std::runtime_error("Error al asignar argumentos del kernel OpenCL");
    }

    // Bloques 16x16 (256 work-items) alineados con la Figura 2 del Avance 1 y el hardware gfx90c.
    const std::size_t local[2] = {16U, 16U};
    const std::size_t global[2] = {
        round_up(static_cast<std::size_t>(width), 16U),
        round_up(static_cast<std::size_t>(height), 16U),
    };

    rc = cl.clEnqueueNDRangeKernel(g.queue, g.kernel, 2, nullptr, global, local, 0, nullptr, nullptr);
    if (rc != gpu::CL_SUCCESS) {
        throw std::runtime_error("Error al encolar kernel OpenCL");
    }

    rc = cl.clEnqueueReadBuffer(g.queue, g.buf_out, 1 /* CL_TRUE */, 0, bytes, output.data.data(), 0, nullptr, nullptr);
    if (rc != gpu::CL_SUCCESS) {
        throw std::runtime_error("Error al leer buffer de salida desde GPU");
    }
    cl.clFinish(g.queue);
}

ImageRGBA sobel_gpu(const ImageRGBA& input) {
    if (!input.is_valid()) {
        throw std::invalid_argument("Input image is not valid");
    }
    ImageRGBA output(input.width, input.height);
    sobel_gpu_into(input, output);
    return output;
}

}  // namespace sobel

#include "sobel_gpu.hpp"

#include "opencl_loader.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace sobel {

namespace {

// Kernel OpenCL preliminar (Propuesta 1: memoria global, 1 work-item por píxel).
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

std::string to_lower_copy(const std::string& text) {
    std::string out = text;
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return out;
}

bool contains_ci(const std::string& haystack, const std::string& needle) {
    return to_lower_copy(haystack).find(to_lower_copy(needle)) != std::string::npos;
}

struct EnumeratedGpu {
    gpu::cl_device_id device = nullptr;
    GpuDeviceInfo info;
};

std::vector<EnumeratedGpu> enumerate_gpus(const gpu::OpenCLApi& cl) {
    std::vector<EnumeratedGpu> result;
    gpu::cl_uint num_platforms = 0;
    if (cl.clGetPlatformIDs(0, nullptr, &num_platforms) != gpu::CL_SUCCESS || num_platforms == 0) {
        return result;
    }

    std::vector<gpu::cl_platform_id> platforms(num_platforms);
    if (cl.clGetPlatformIDs(num_platforms, platforms.data(), nullptr) != gpu::CL_SUCCESS) {
        return result;
    }

    int global_idx = 0;
    for (gpu::cl_platform_id plat : platforms) {
        gpu::cl_uint num_gpus = 0;
        if (cl.clGetDeviceIDs(plat, gpu::CL_DEVICE_TYPE_GPU, 0, nullptr, &num_gpus) != gpu::CL_SUCCESS ||
            num_gpus == 0) {
            continue;
        }

        std::vector<gpu::cl_device_id> devices(num_gpus);
        if (cl.clGetDeviceIDs(plat, gpu::CL_DEVICE_TYPE_GPU, num_gpus, devices.data(), nullptr) != gpu::CL_SUCCESS) {
            continue;
        }

        const std::string plat_name = gpu::get_platform_string(cl, plat, gpu::CL_PLATFORM_NAME);
        for (gpu::cl_device_id dev : devices) {
            EnumeratedGpu entry;
            entry.device = dev;
            entry.info.index = global_idx++;
            entry.info.platform_name = plat_name;
            entry.info.device_name = gpu::get_device_string(cl, dev, gpu::CL_DEVICE_NAME);
            entry.info.device_vendor = gpu::get_device_string(cl, dev, gpu::CL_DEVICE_VENDOR);
            entry.info.opencl_version = gpu::get_device_string(cl, dev, gpu::CL_DEVICE_VERSION);

            gpu::cl_uint cu = 0;
            if (cl.clGetDeviceInfo(dev, gpu::CL_DEVICE_MAX_COMPUTE_UNITS, sizeof(cu), &cu, nullptr) == gpu::CL_SUCCESS) {
                entry.info.compute_units = cu;
            }
            std::size_t max_wg = 0;
            if (cl.clGetDeviceInfo(dev, gpu::CL_DEVICE_MAX_WORK_GROUP_SIZE, sizeof(max_wg), &max_wg, nullptr) ==
                gpu::CL_SUCCESS) {
                entry.info.max_work_group_size = max_wg;
            }
            gpu::cl_ulong local_mem = 0;
            if (cl.clGetDeviceInfo(dev, gpu::CL_DEVICE_LOCAL_MEM_SIZE, sizeof(local_mem), &local_mem, nullptr) ==
                gpu::CL_SUCCESS) {
                entry.info.local_mem_size_bytes = local_mem;
            }
            result.push_back(std::move(entry));
        }
    }
    return result;
}

int select_gpu_index(const std::vector<EnumeratedGpu>& gpus, int requested_index) {
    if (gpus.empty()) {
        throw std::runtime_error("No se encontro ningun dispositivo GPU OpenCL");
    }
    if (requested_index >= 0) {
        if (static_cast<std::size_t>(requested_index) >= gpus.size()) {
            throw std::runtime_error("Indice de GPU OpenCL fuera de rango: " + std::to_string(requested_index) +
                                     " (disponibles: " + std::to_string(gpus.size()) + ")");
        }
        return requested_index;
    }

    // Prioridad 1: Intel Iris Xe (entorno objetivo del proyecto).
    for (std::size_t i = 0; i < gpus.size(); ++i) {
        if (contains_ci(gpus[i].info.device_name, "iris")) {
            return static_cast<int>(i);
        }
    }
    // Prioridad 2: Cualquier GPU Intel (UHD / Iris / Arc).
    for (std::size_t i = 0; i < gpus.size(); ++i) {
        if (contains_ci(gpus[i].info.device_vendor, "intel") ||
            contains_ci(gpus[i].info.platform_name, "intel") ||
            contains_ci(gpus[i].info.device_name, "intel")) {
            return static_cast<int>(i);
        }
    }
    // Fallback: Primera GPU OpenCL disponible (AMD, NVIDIA, etc.).
    return 0;
}

struct EventGuard {
    const gpu::OpenCLApi& cl;
    gpu::cl_event ev = nullptr;

    explicit EventGuard(const gpu::OpenCLApi& api) : cl(api) {}

    ~EventGuard() {
        if (ev) {
            cl.clReleaseEvent(ev);
        }
    }
};

double event_duration_ms(const gpu::OpenCLApi& cl, gpu::cl_event ev) {
    if (!ev) {
        return 0.0;
    }
    gpu::cl_ulong start_ns = 0;
    gpu::cl_ulong end_ns = 0;
    const gpu::cl_int rc1 =
        cl.clGetEventProfilingInfo(ev, gpu::CL_PROFILING_COMMAND_START, sizeof(start_ns), &start_ns, nullptr);
    const gpu::cl_int rc2 =
        cl.clGetEventProfilingInfo(ev, gpu::CL_PROFILING_COMMAND_END, sizeof(end_ns), &end_ns, nullptr);
    if (rc1 != gpu::CL_SUCCESS || rc2 != gpu::CL_SUCCESS || end_ns < start_ns) {
        return 0.0;
    }
    return static_cast<double>(end_ns - start_ns) * 1e-6;
}

}  // namespace

struct SobelGpuContext::Impl {
    gpu::OpenCLApi cl;
    gpu::cl_device_id device = nullptr;
    GpuDeviceInfo info;

    gpu::cl_context ctx = nullptr;
    gpu::cl_command_queue queue = nullptr;
    gpu::cl_program program = nullptr;
    gpu::cl_kernel kernel_naive = nullptr;

    // Buffers reutilizables para un tamaño (buf_width, buf_height) dado.
    gpu::cl_mem buf_in = nullptr;
    gpu::cl_mem buf_out = nullptr;
    int buf_width = 0;
    int buf_height = 0;

    void release_buffers() noexcept {
        if (buf_out) {
            cl.clReleaseMemObject(buf_out);
            buf_out = nullptr;
        }
        if (buf_in) {
            cl.clReleaseMemObject(buf_in);
            buf_in = nullptr;
        }
        buf_width = 0;
        buf_height = 0;
    }

    void cleanup() noexcept {
        release_buffers();
        if (kernel_naive) {
            cl.clReleaseKernel(kernel_naive);
            kernel_naive = nullptr;
        }
        if (program) {
            cl.clReleaseProgram(program);
            program = nullptr;
        }
        if (queue) {
            cl.clReleaseCommandQueue(queue);
            queue = nullptr;
        }
        if (ctx) {
            cl.clReleaseContext(ctx);
            ctx = nullptr;
        }
        device = nullptr;
        cl.unload();
    }

    ~Impl() {
        cleanup();
    }
};

SobelGpuContext::SobelGpuContext(int device_index) : impl_(std::make_unique<Impl>()) {
    std::string err;
    if (!impl_->cl.load(err)) {
        throw std::runtime_error(err);
    }

    try {
        const auto gpus = enumerate_gpus(impl_->cl);
        const int chosen = select_gpu_index(gpus, device_index);
        impl_->device = gpus[static_cast<std::size_t>(chosen)].device;
        impl_->info = gpus[static_cast<std::size_t>(chosen)].info;

        gpu::cl_int rc = gpu::CL_SUCCESS;
        impl_->ctx = impl_->cl.clCreateContext(nullptr, 1, &impl_->device, nullptr, nullptr, &rc);
        if (!impl_->ctx || rc != gpu::CL_SUCCESS) {
            throw std::runtime_error("Error al crear contexto OpenCL");
        }

        // Habilita profiling por eventos de hardware en la cola de comandos.
        impl_->queue = impl_->cl.clCreateCommandQueue(impl_->ctx, impl_->device,
                                                      gpu::CL_QUEUE_PROFILING_ENABLE, &rc);
        if (!impl_->queue || rc != gpu::CL_SUCCESS) {
            throw std::runtime_error("Error al crear command queue OpenCL con CL_QUEUE_PROFILING_ENABLE");
        }

        const char* src_ptr = kSobelNaiveKernelSource;
        impl_->program = impl_->cl.clCreateProgramWithSource(impl_->ctx, 1, &src_ptr, nullptr, &rc);
        if (!impl_->program || rc != gpu::CL_SUCCESS) {
            throw std::runtime_error("Error al crear programa OpenCL");
        }

        rc = impl_->cl.clBuildProgram(impl_->program, 1, &impl_->device, nullptr, nullptr, nullptr);
        if (rc != gpu::CL_SUCCESS) {
            std::size_t log_size = 0;
            impl_->cl.clGetProgramBuildInfo(impl_->program, impl_->device, gpu::CL_PROGRAM_BUILD_LOG,
                                            0, nullptr, &log_size);
            std::string log(log_size, '\0');
            if (log_size > 0) {
                impl_->cl.clGetProgramBuildInfo(impl_->program, impl_->device, gpu::CL_PROGRAM_BUILD_LOG,
                                                log_size, log.data(), nullptr);
            }
            throw std::runtime_error("Error compilando kernel OpenCL:\n" + log);
        }

        impl_->kernel_naive = impl_->cl.clCreateKernel(impl_->program, "sobel_rgba_naive", &rc);
        if (!impl_->kernel_naive || rc != gpu::CL_SUCCESS) {
            throw std::runtime_error("Error al crear kernel sobel_rgba_naive");
        }
    } catch (...) {
        impl_->cleanup();
        throw;
    }
}

SobelGpuContext::~SobelGpuContext() = default;
SobelGpuContext::SobelGpuContext(SobelGpuContext&&) noexcept = default;
SobelGpuContext& SobelGpuContext::operator=(SobelGpuContext&&) noexcept = default;

std::vector<GpuDeviceInfo> SobelGpuContext::list_gpu_devices() {
    gpu::OpenCLApi cl;
    std::string err;
    if (!cl.load(err)) {
        throw std::runtime_error(err);
    }

    struct ApiUnloader {
        gpu::OpenCLApi& api;
        explicit ApiUnloader(gpu::OpenCLApi& api_ref) : api(api_ref) {}
        ~ApiUnloader() { api.unload(); }
    } unloader(cl);

    const auto gpus = enumerate_gpus(cl);
    std::vector<GpuDeviceInfo> infos;
    infos.reserve(gpus.size());
    for (const auto& entry : gpus) {
        infos.push_back(entry.info);
    }
    return infos;
}

const GpuDeviceInfo& SobelGpuContext::device_info() const {
    if (!impl_ || !impl_->ctx) {
        throw std::runtime_error("SobelGpuContext no esta inicializado");
    }
    return impl_->info;
}

void SobelGpuContext::ensure_buffers(int width, int height) {
    if (!impl_ || !impl_->ctx) {
        throw std::runtime_error("SobelGpuContext no esta inicializado");
    }
    if (width <= 0 || height <= 0) {
        throw std::invalid_argument("Dimensiones invalidas para buffers GPU");
    }
    if (impl_->buf_in && impl_->buf_out && impl_->buf_width == width && impl_->buf_height == height) {
        return;
    }

    impl_->release_buffers();
    const std::size_t bytes =
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U;

    gpu::cl_int rc = gpu::CL_SUCCESS;
    impl_->buf_in = impl_->cl.clCreateBuffer(impl_->ctx, gpu::CL_MEM_READ_ONLY, bytes, nullptr, &rc);
    if (!impl_->buf_in || rc != gpu::CL_SUCCESS) {
        impl_->release_buffers();
        throw std::runtime_error("Error al crear buffer de entrada en GPU");
    }

    impl_->buf_out = impl_->cl.clCreateBuffer(impl_->ctx, gpu::CL_MEM_WRITE_ONLY, bytes, nullptr, &rc);
    if (!impl_->buf_out || rc != gpu::CL_SUCCESS) {
        impl_->release_buffers();
        throw std::runtime_error("Error al crear buffer de salida en GPU");
    }

    impl_->buf_width = width;
    impl_->buf_height = height;
}

double SobelGpuContext::upload(const ImageRGBA& input) {
    if (!input.is_valid()) {
        throw std::invalid_argument("Input image is not valid");
    }
    ensure_buffers(input.width, input.height);

    EventGuard ev_h2d{impl_->cl};
    const gpu::cl_int rc = impl_->cl.clEnqueueWriteBuffer(
        impl_->queue, impl_->buf_in, gpu::CL_TRUE, 0, input.byte_size(),
        input.data.data(), 0, nullptr, &ev_h2d.ev);
    if (rc != gpu::CL_SUCCESS) {
        throw std::runtime_error("Error al transferir buffer Host -> Device");
    }
    return event_duration_ms(impl_->cl, ev_h2d.ev);
}

double SobelGpuContext::execute_kernel(int width, int height) {
    if (!impl_ || !impl_->ctx) {
        throw std::runtime_error("SobelGpuContext no esta inicializado");
    }
    if (!impl_->buf_in || !impl_->buf_out || impl_->buf_width != width || impl_->buf_height != height) {
        throw std::runtime_error("Los buffers GPU no coinciden con las dimensiones solicitadas");
    }

    gpu::cl_int rc = impl_->cl.clSetKernelArg(impl_->kernel_naive, 0, sizeof(gpu::cl_mem), &impl_->buf_in);
    rc |= impl_->cl.clSetKernelArg(impl_->kernel_naive, 1, sizeof(gpu::cl_mem), &impl_->buf_out);
    rc |= impl_->cl.clSetKernelArg(impl_->kernel_naive, 2, sizeof(int), &width);
    rc |= impl_->cl.clSetKernelArg(impl_->kernel_naive, 3, sizeof(int), &height);
    if (rc != gpu::CL_SUCCESS) {
        throw std::runtime_error("Error al asignar argumentos del kernel OpenCL");
    }

    // Work-groups 16x16 (256 work-items) con redondeo del Global Work Size.
    const std::size_t local[2] = {16U, 16U};
    const std::size_t global[2] = {
        round_up(static_cast<std::size_t>(width), 16U),
        round_up(static_cast<std::size_t>(height), 16U),
    };

    EventGuard ev_kernel{impl_->cl};
    rc = impl_->cl.clEnqueueNDRangeKernel(
        impl_->queue, impl_->kernel_naive, 2, nullptr, global, local, 0, nullptr, &ev_kernel.ev);
    if (rc != gpu::CL_SUCCESS) {
        throw std::runtime_error("Error al encolar kernel OpenCL");
    }
    rc = impl_->cl.clWaitForEvents(1, &ev_kernel.ev);
    if (rc != gpu::CL_SUCCESS) {
        throw std::runtime_error("Error esperando evento de ejecucion del kernel OpenCL");
    }
    return event_duration_ms(impl_->cl, ev_kernel.ev);
}

double SobelGpuContext::download(ImageRGBA& output) {
    if (!impl_ || !impl_->ctx) {
        throw std::runtime_error("SobelGpuContext no esta inicializado");
    }
    if (!output.is_valid() || output.width != impl_->buf_width || output.height != impl_->buf_height) {
        throw std::invalid_argument("Output image dimensions must match GPU buffer dimensions");
    }

    EventGuard ev_d2h{impl_->cl};
    const gpu::cl_int rc = impl_->cl.clEnqueueReadBuffer(
        impl_->queue, impl_->buf_out, gpu::CL_TRUE, 0, output.byte_size(),
        output.data.data(), 0, nullptr, &ev_d2h.ev);
    if (rc != gpu::CL_SUCCESS) {
        throw std::runtime_error("Error al leer buffer Device -> Host");
    }
    return event_duration_ms(impl_->cl, ev_d2h.ev);
}

void SobelGpuContext::run(const ImageRGBA& input, ImageRGBA& output, GpuTimingResult* timing) {
    if (!input.is_valid()) {
        throw std::invalid_argument("Input image is not valid");
    }
    if (output.width != input.width || output.height != input.height || !output.is_valid()) {
        throw std::invalid_argument("Output image dimensions must match input image");
    }

    ensure_buffers(input.width, input.height);

    const int width = input.width;
    const int height = input.height;
    gpu::cl_int rc = impl_->cl.clSetKernelArg(impl_->kernel_naive, 0, sizeof(gpu::cl_mem), &impl_->buf_in);
    rc |= impl_->cl.clSetKernelArg(impl_->kernel_naive, 1, sizeof(gpu::cl_mem), &impl_->buf_out);
    rc |= impl_->cl.clSetKernelArg(impl_->kernel_naive, 2, sizeof(int), &width);
    rc |= impl_->cl.clSetKernelArg(impl_->kernel_naive, 3, sizeof(int), &height);
    if (rc != gpu::CL_SUCCESS) {
        throw std::runtime_error("Error al asignar argumentos del kernel OpenCL");
    }

    const std::size_t local[2] = {16U, 16U};
    const std::size_t global[2] = {
        round_up(static_cast<std::size_t>(width), 16U),
        round_up(static_cast<std::size_t>(height), 16U),
    };

    EventGuard ev_h2d{impl_->cl};
    EventGuard ev_kernel{impl_->cl};
    EventGuard ev_d2h{impl_->cl};

    const auto t0 = std::chrono::steady_clock::now();

    rc = impl_->cl.clEnqueueWriteBuffer(
        impl_->queue, impl_->buf_in, gpu::CL_FALSE, 0, input.byte_size(),
        input.data.data(), 0, nullptr, &ev_h2d.ev);
    if (rc != gpu::CL_SUCCESS) {
        throw std::runtime_error("Error al encolar copia Host -> Device");
    }

    rc = impl_->cl.clEnqueueNDRangeKernel(
        impl_->queue, impl_->kernel_naive, 2, nullptr, global, local, 0, nullptr, &ev_kernel.ev);
    if (rc != gpu::CL_SUCCESS) {
        throw std::runtime_error("Error al encolar kernel OpenCL");
    }

    rc = impl_->cl.clEnqueueReadBuffer(
        impl_->queue, impl_->buf_out, gpu::CL_FALSE, 0, output.byte_size(),
        output.data.data(), 0, nullptr, &ev_d2h.ev);
    if (rc != gpu::CL_SUCCESS) {
        throw std::runtime_error("Error al encolar copia Device -> Host");
    }

    rc = impl_->cl.clFinish(impl_->queue);
    if (rc != gpu::CL_SUCCESS) {
        throw std::runtime_error("Error en clFinish de la cola OpenCL");
    }

    const auto t1 = std::chrono::steady_clock::now();

    if (timing) {
        timing->h2d_ms = event_duration_ms(impl_->cl, ev_h2d.ev);
        timing->kernel_ms = event_duration_ms(impl_->cl, ev_kernel.ev);
        timing->d2h_ms = event_duration_ms(impl_->cl, ev_d2h.ev);
        timing->total_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    }
}

void SobelGpuContext::cleanup() noexcept {
    if (impl_) {
        impl_->cleanup();
    }
}

void sobel_gpu_into(SobelGpuContext& ctx, const ImageRGBA& input, ImageRGBA& output,
                    GpuTimingResult* timing) {
    ctx.run(input, output, timing);
}

void sobel_gpu_into(const ImageRGBA& input, ImageRGBA& output) {
    SobelGpuContext ctx;
    ctx.run(input, output);
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

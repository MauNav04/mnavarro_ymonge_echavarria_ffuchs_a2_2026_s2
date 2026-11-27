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

const char* gpu_kernel_variant_name(GpuKernelVariant variant) noexcept {
    switch (variant) {
        case GpuKernelVariant::Naive:
            return "gpu_naive";
        case GpuKernelVariant::Tiled18x18:
            return "gpu_tiled";
    }
    return "unknown";
}

namespace {

// Kernels OpenCL 1.2 coalescentes con uchar4:
//  - sobel_rgba_naive: lectura directa desde memoria global (1 transaccion de 32 bits/pixel).
//  - sobel_rgba_tiled: carga cooperativa de tile 18x18 (1296 bytes) en memoria local (SLM) + barrier.
constexpr const char* kSobelKernelsSource = R"CLC(
static inline uchar4 load_zero_padded(
    __global const uchar4* input,
    const int x,
    const int y,
    const int width,
    const int height)
{
    if (x < 0 || x >= width || y < 0 || y >= height) {
        return (uchar4)(0, 0, 0, 0);
    }
    return input[y * width + x];
}

static inline uchar4 compute_sobel_pixel(
    const uchar4 tl, const uchar4 tc, const uchar4 tr,
    const uchar4 ml,                  const uchar4 mr,
    const uchar4 bl, const uchar4 bc, const uchar4 br)
{
    const int3 p_tl = convert_int3(tl.xyz);
    const int3 p_tc = convert_int3(tc.xyz);
    const int3 p_tr = convert_int3(tr.xyz);
    const int3 p_ml = convert_int3(ml.xyz);
    const int3 p_mr = convert_int3(mr.xyz);
    const int3 p_bl = convert_int3(bl.xyz);
    const int3 p_bc = convert_int3(bc.xyz);
    const int3 p_br = convert_int3(br.xyz);

    // Kx = [[-1, 0, 1], [-2, 0, 2], [-1, 0, 1]]
    const int3 gx = (p_tr - p_tl) + ((p_mr - p_ml) * 2) + (p_br - p_bl);

    // Ky = [[-1, -2, -1], [0, 0, 0], [1, 2, 1]]
    const int3 gy = (p_bl - p_tl) + ((p_bc - p_tc) * 2) + (p_br - p_tr);

    // Norma L1 (Manhattan) saturada a [0, 255]; canal alfa fijo en 255.
    const uint3 mag = min(abs(gx) + abs(gy), (uint3)(255U));
    return (uchar4)(convert_uchar3(mag), (uchar)255);
}

// Propuesta 1: kernel coalescente en memoria global (uchar4, 1 work-item por pixel).
__attribute__((reqd_work_group_size(16, 16, 1)))
__kernel void sobel_rgba_naive(
    __global const uchar4* input,
    __global uchar4* output,
    const int width,
    const int height)
{
    const int x = (int)get_global_id(0);
    const int y = (int)get_global_id(1);
    if (x >= width || y >= height) {
        return;
    }

    const uchar4 tl = load_zero_padded(input, x - 1, y - 1, width, height);
    const uchar4 tc = load_zero_padded(input, x,     y - 1, width, height);
    const uchar4 tr = load_zero_padded(input, x + 1, y - 1, width, height);
    const uchar4 ml = load_zero_padded(input, x - 1, y,     width, height);
    const uchar4 mr = load_zero_padded(input, x + 1, y,     width, height);
    const uchar4 bl = load_zero_padded(input, x - 1, y + 1, width, height);
    const uchar4 bc = load_zero_padded(input, x,     y + 1, width, height);
    const uchar4 br = load_zero_padded(input, x + 1, y + 1, width, height);

    output[y * width + x] = compute_sobel_pixel(tl, tc, tr, ml, mr, bl, bc, br);
}

// Propuesta 2: kernel con tiling cooperativo 18x18 (halo de 1 pixel) en memoria local (SLM).
__attribute__((reqd_work_group_size(16, 16, 1)))
__kernel void sobel_rgba_tiled(
    __global const uchar4* input,
    __global uchar4* output,
    const int width,
    const int height)
{
    __local uchar4 tile[18][18];

    const int lx = (int)get_local_id(0);
    const int ly = (int)get_local_id(1);
    const int group_x = (int)get_group_id(0) * 16;
    const int group_y = (int)get_group_id(1) * 16;

    // Carga cooperativa de los 324 pixeles (18x18) sin return temprano para evitar deadlocks.
    const int lid = ly * 16 + lx;
    for (int idx = lid; idx < 324; idx += 256) {
        const int ty = idx / 18;
        const int tx = idx - ty * 18;
        const int gx = group_x + tx - 1;
        const int gy = group_y + ty - 1;

        if (gx >= 0 && gx < width && gy >= 0 && gy < height) {
            tile[ty][tx] = input[gy * width + gx];
        } else {
            tile[ty][tx] = (uchar4)(0, 0, 0, 0);
        }
    }

    // Todos los 256 hilos del work-group alcanzan esta barrera.
    barrier(CLK_LOCAL_MEM_FENCE);

    const int x = group_x + lx;
    const int y = group_y + ly;
    if (x >= width || y >= height) {
        return;
    }

    const uchar4 tl = tile[ly + 0][lx + 0];
    const uchar4 tc = tile[ly + 0][lx + 1];
    const uchar4 tr = tile[ly + 0][lx + 2];
    const uchar4 ml = tile[ly + 1][lx + 0];
    const uchar4 mr = tile[ly + 1][lx + 2];
    const uchar4 bl = tile[ly + 2][lx + 0];
    const uchar4 bc = tile[ly + 2][lx + 1];
    const uchar4 br = tile[ly + 2][lx + 2];

    output[y * width + x] = compute_sobel_pixel(tl, tc, tr, ml, mr, bl, bc, br);
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

std::string trim_copy(const std::string& text) {
    std::size_t start = 0;
    while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start]))) {
        ++start;
    }
    std::size_t end = text.size();
    while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1]))) {
        --end;
    }
    return text.substr(start, end - start);
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
    std::string program_build_log;

    gpu::cl_context ctx = nullptr;
    gpu::cl_command_queue queue = nullptr;
    gpu::cl_program program = nullptr;
    gpu::cl_kernel kernel_naive = nullptr;
    gpu::cl_kernel kernel_tiled = nullptr;

    // Buffers reutilizables para un tamaño (buf_width, buf_height) dado.
    gpu::cl_mem buf_in = nullptr;
    gpu::cl_mem buf_out = nullptr;
    int buf_width = 0;
    int buf_height = 0;

    gpu::cl_kernel select_kernel(GpuKernelVariant variant) const {
        switch (variant) {
            case GpuKernelVariant::Naive:
                return kernel_naive;
            case GpuKernelVariant::Tiled18x18:
                return kernel_tiled;
        }
        throw std::invalid_argument("Variante de kernel GPU desconocida");
    }

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
        if (kernel_tiled) {
            cl.clReleaseKernel(kernel_tiled);
            kernel_tiled = nullptr;
        }
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

        const char* src_ptr = kSobelKernelsSource;
        impl_->program = impl_->cl.clCreateProgramWithSource(impl_->ctx, 1, &src_ptr, nullptr, &rc);
        if (!impl_->program || rc != gpu::CL_SUCCESS) {
            throw std::runtime_error("Error al crear programa OpenCL");
        }

        rc = impl_->cl.clBuildProgram(impl_->program, 1, &impl_->device, nullptr, nullptr, nullptr);

        // Captura el build log incluso si compila con éxito para inspeccionar posibles warnings.
        std::size_t log_size = 0;
        impl_->cl.clGetProgramBuildInfo(impl_->program, impl_->device, gpu::CL_PROGRAM_BUILD_LOG,
                                        0, nullptr, &log_size);
        if (log_size > 0) {
            std::string raw_log(log_size, '\0');
            impl_->cl.clGetProgramBuildInfo(impl_->program, impl_->device, gpu::CL_PROGRAM_BUILD_LOG,
                                            log_size, raw_log.data(), nullptr);
            while (!raw_log.empty() && raw_log.back() == '\0') {
                raw_log.pop_back();
            }
            impl_->program_build_log = trim_copy(raw_log);
        }

        if (rc != gpu::CL_SUCCESS) {
            throw std::runtime_error("Error compilando kernels OpenCL:\n" + impl_->program_build_log);
        }

        impl_->kernel_naive = impl_->cl.clCreateKernel(impl_->program, "sobel_rgba_naive", &rc);
        if (!impl_->kernel_naive || rc != gpu::CL_SUCCESS) {
            throw std::runtime_error("Error al crear kernel sobel_rgba_naive");
        }

        impl_->kernel_tiled = impl_->cl.clCreateKernel(impl_->program, "sobel_rgba_tiled", &rc);
        if (!impl_->kernel_tiled || rc != gpu::CL_SUCCESS) {
            throw std::runtime_error("Error al crear kernel sobel_rgba_tiled");
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

const std::string& SobelGpuContext::build_log() const {
    if (!impl_ || !impl_->ctx) {
        throw std::runtime_error("SobelGpuContext no esta inicializado");
    }
    return impl_->program_build_log;
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

double SobelGpuContext::execute_kernel(int width, int height, GpuKernelVariant variant) {
    if (!impl_ || !impl_->ctx) {
        throw std::runtime_error("SobelGpuContext no esta inicializado");
    }
    if (!impl_->buf_in || !impl_->buf_out || impl_->buf_width != width || impl_->buf_height != height) {
        throw std::runtime_error("Los buffers GPU no coinciden con las dimensiones solicitadas");
    }

    gpu::cl_kernel kernel = impl_->select_kernel(variant);
    gpu::cl_int rc = impl_->cl.clSetKernelArg(kernel, 0, sizeof(gpu::cl_mem), &impl_->buf_in);
    rc |= impl_->cl.clSetKernelArg(kernel, 1, sizeof(gpu::cl_mem), &impl_->buf_out);
    rc |= impl_->cl.clSetKernelArg(kernel, 2, sizeof(int), &width);
    rc |= impl_->cl.clSetKernelArg(kernel, 3, sizeof(int), &height);
    if (rc != gpu::CL_SUCCESS) {
        throw std::runtime_error("Error al asignar argumentos del kernel OpenCL");
    }

    // Work-groups fijos 16x16 (256 work-items) con redondeo seguro del Global Work Size.
    const std::size_t local[2] = {16U, 16U};
    const std::size_t global[2] = {
        round_up(static_cast<std::size_t>(width), 16U),
        round_up(static_cast<std::size_t>(height), 16U),
    };

    EventGuard ev_kernel{impl_->cl};
    rc = impl_->cl.clEnqueueNDRangeKernel(
        impl_->queue, kernel, 2, nullptr, global, local, 0, nullptr, &ev_kernel.ev);
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

void SobelGpuContext::run(const ImageRGBA& input, ImageRGBA& output,
                          GpuKernelVariant variant, GpuTimingResult* timing) {
    if (!input.is_valid()) {
        throw std::invalid_argument("Input image is not valid");
    }
    if (output.width != input.width || output.height != input.height || !output.is_valid()) {
        throw std::invalid_argument("Output image dimensions must match input image");
    }

    ensure_buffers(input.width, input.height);

    const int width = input.width;
    const int height = input.height;
    gpu::cl_kernel kernel = impl_->select_kernel(variant);
    gpu::cl_int rc = impl_->cl.clSetKernelArg(kernel, 0, sizeof(gpu::cl_mem), &impl_->buf_in);
    rc |= impl_->cl.clSetKernelArg(kernel, 1, sizeof(gpu::cl_mem), &impl_->buf_out);
    rc |= impl_->cl.clSetKernelArg(kernel, 2, sizeof(int), &width);
    rc |= impl_->cl.clSetKernelArg(kernel, 3, sizeof(int), &height);
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
        impl_->queue, kernel, 2, nullptr, global, local, 0, nullptr, &ev_kernel.ev);
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
                    GpuKernelVariant variant, GpuTimingResult* timing) {
    ctx.run(input, output, variant, timing);
}

void sobel_gpu_into(const ImageRGBA& input, ImageRGBA& output, GpuKernelVariant variant) {
    SobelGpuContext ctx;
    ctx.run(input, output, variant);
}

ImageRGBA sobel_gpu(const ImageRGBA& input, GpuKernelVariant variant) {
    if (!input.is_valid()) {
        throw std::invalid_argument("Input image is not valid");
    }
    ImageRGBA output(input.width, input.height);
    sobel_gpu_into(input, output, variant);
    return output;
}

}  // namespace sobel

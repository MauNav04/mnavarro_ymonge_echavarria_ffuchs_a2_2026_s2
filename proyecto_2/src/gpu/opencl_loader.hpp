#ifndef SOBEL_DLP_OPENCL_LOADER_HPP
#define SOBEL_DLP_OPENCL_LOADER_HPP

#include <cstddef>
#include <cstdint>
#include <string>

#ifdef _WIN32
#define CL_API_CALL __stdcall
#else
#define CL_API_CALL
#endif

namespace sobel::gpu {

using cl_int = int32_t;
using cl_uint = uint32_t;
using cl_ulong = uint64_t;
using cl_bitfield = uint64_t;
using cl_bool = cl_uint;

using cl_platform_id = struct _cl_platform_id*;
using cl_device_id = struct _cl_device_id*;
using cl_context = struct _cl_context*;
using cl_command_queue = struct _cl_command_queue*;
using cl_mem = struct _cl_mem*;
using cl_program = struct _cl_program*;
using cl_kernel = struct _cl_kernel*;
using cl_event = struct _cl_event*;

using cl_platform_info = cl_uint;
using cl_device_info = cl_uint;
using cl_device_type = cl_bitfield;
using cl_context_properties = intptr_t;
using cl_command_queue_properties = cl_bitfield;
using cl_mem_flags = cl_bitfield;
using cl_program_build_info = cl_uint;

constexpr cl_int CL_SUCCESS = 0;
constexpr cl_device_type CL_DEVICE_TYPE_CPU = (1ULL << 1);
constexpr cl_device_type CL_DEVICE_TYPE_GPU = (1ULL << 2);
constexpr cl_device_type CL_DEVICE_TYPE_ALL = 0xFFFFFFFFULL;

constexpr cl_platform_info CL_PLATFORM_NAME = 0x0902;
constexpr cl_platform_info CL_PLATFORM_VENDOR = 0x0903;

constexpr cl_device_info CL_DEVICE_TYPE = 0x1000;
constexpr cl_device_info CL_DEVICE_MAX_COMPUTE_UNITS = 0x1002;
constexpr cl_device_info CL_DEVICE_MAX_WORK_GROUP_SIZE = 0x1004;
constexpr cl_device_info CL_DEVICE_LOCAL_MEM_SIZE = 0x1023;
constexpr cl_device_info CL_DEVICE_NAME = 0x102B;
constexpr cl_device_info CL_DEVICE_VENDOR = 0x102C;
constexpr cl_device_info CL_DEVICE_VERSION = 0x102F;

constexpr cl_mem_flags CL_MEM_READ_WRITE = (1ULL << 0);
constexpr cl_mem_flags CL_MEM_WRITE_ONLY = (1ULL << 1);
constexpr cl_mem_flags CL_MEM_READ_ONLY = (1ULL << 2);
constexpr cl_mem_flags CL_MEM_COPY_HOST_PTR = (1ULL << 5);

constexpr cl_program_build_info CL_PROGRAM_BUILD_LOG = 0x1183;

/**
 * @brief Cargador dinámico de funciones estándar OpenCL 1.2 (Windows OpenCL.dll / Linux libOpenCL.so).
 *
 * Permite compilar y ejecutar en hardware real sin requerir headers o SDKs propietarios instalados.
 */
struct OpenCLApi {
    void* lib_handle = nullptr;

    cl_int(CL_API_CALL* clGetPlatformIDs)(cl_uint, cl_platform_id*, cl_uint*) = nullptr;
    cl_int(CL_API_CALL* clGetPlatformInfo)(cl_platform_id, cl_platform_info, std::size_t, void*, std::size_t*) = nullptr;
    cl_int(CL_API_CALL* clGetDeviceIDs)(cl_platform_id, cl_device_type, cl_uint, cl_device_id*, cl_uint*) = nullptr;
    cl_int(CL_API_CALL* clGetDeviceInfo)(cl_device_id, cl_device_info, std::size_t, void*, std::size_t*) = nullptr;

    cl_context(CL_API_CALL* clCreateContext)(const cl_context_properties*, cl_uint, const cl_device_id*,
                                             void(CL_API_CALL*)(const char*, const void*, std::size_t, void*),
                                             void*, cl_int*) = nullptr;
    cl_int(CL_API_CALL* clReleaseContext)(cl_context) = nullptr;

    cl_command_queue(CL_API_CALL* clCreateCommandQueue)(cl_context, cl_device_id, cl_command_queue_properties, cl_int*) = nullptr;
    cl_int(CL_API_CALL* clReleaseCommandQueue)(cl_command_queue) = nullptr;
    cl_int(CL_API_CALL* clFinish)(cl_command_queue) = nullptr;

    cl_mem(CL_API_CALL* clCreateBuffer)(cl_context, cl_mem_flags, std::size_t, void*, cl_int*) = nullptr;
    cl_int(CL_API_CALL* clReleaseMemObject)(cl_mem) = nullptr;
    cl_int(CL_API_CALL* clEnqueueReadBuffer)(cl_command_queue, cl_mem, cl_bool, std::size_t, std::size_t,
                                             void*, cl_uint, const cl_event*, cl_event*) = nullptr;
    cl_int(CL_API_CALL* clEnqueueWriteBuffer)(cl_command_queue, cl_mem, cl_bool, std::size_t, std::size_t,
                                              const void*, cl_uint, const cl_event*, cl_event*) = nullptr;

    cl_program(CL_API_CALL* clCreateProgramWithSource)(cl_context, cl_uint, const char**, const std::size_t*, cl_int*) = nullptr;
    cl_int(CL_API_CALL* clBuildProgram)(cl_program, cl_uint, const cl_device_id*, const char*,
                                        void(CL_API_CALL*)(cl_program, void*), void*) = nullptr;
    cl_int(CL_API_CALL* clGetProgramBuildInfo)(cl_program, cl_device_id, cl_program_build_info,
                                               std::size_t, void*, std::size_t*) = nullptr;
    cl_int(CL_API_CALL* clReleaseProgram)(cl_program) = nullptr;

    cl_kernel(CL_API_CALL* clCreateKernel)(cl_program, const char*, cl_int*) = nullptr;
    cl_int(CL_API_CALL* clSetKernelArg)(cl_kernel, cl_uint, std::size_t, const void*) = nullptr;
    cl_int(CL_API_CALL* clEnqueueNDRangeKernel)(cl_command_queue, cl_kernel, cl_uint, const std::size_t*,
                                                const std::size_t*, const std::size_t*, cl_uint,
                                                const cl_event*, cl_event*) = nullptr;
    cl_int(CL_API_CALL* clReleaseKernel)(cl_kernel) = nullptr;

    bool load(std::string& error_out);
    void unload();
};

/**
 * @brief Consulta una cadena de información de plataforma OpenCL.
 */
std::string get_platform_string(const OpenCLApi& api, cl_platform_id platform, cl_platform_info param);

/**
 * @brief Consulta una cadena de información de dispositivo OpenCL.
 */
std::string get_device_string(const OpenCLApi& api, cl_device_id device, cl_device_info param);

}  // namespace sobel::gpu

#endif  // SOBEL_DLP_OPENCL_LOADER_HPP

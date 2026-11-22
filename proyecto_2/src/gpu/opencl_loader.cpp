#include "opencl_loader.hpp"

#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace sobel::gpu {

namespace {

void* get_symbol(void* handle, const char* name) {
#ifdef _WIN32
    return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(handle), name));
#else
    return dlsym(handle, name);
#endif
}

template <typename Fn>
bool bind_fn(void* handle, const char* name, Fn& target, std::string& error_out) {
    void* sym = get_symbol(handle, name);
    if (!sym) {
        error_out = std::string("No se encontro simbolo OpenCL: ") + name;
        return false;
    }
    target = reinterpret_cast<Fn>(sym);
    return true;
}

}  // namespace

bool OpenCLApi::load(std::string& error_out) {
    if (lib_handle) {
        return true;
    }

#ifdef _WIN32
    HMODULE mod = LoadLibraryA("OpenCL.dll");
    if (!mod) {
        error_out = "No se pudo cargar OpenCL.dll";
        return false;
    }
    lib_handle = static_cast<void*>(mod);
#else
    void* mod = dlopen("libOpenCL.so.1", RTLD_LAZY);
    if (!mod) {
        mod = dlopen("libOpenCL.so", RTLD_LAZY);
    }
    if (!mod) {
        error_out = "No se pudo cargar libOpenCL.so";
        return false;
    }
    lib_handle = mod;
#endif

    const bool ok =
        bind_fn(lib_handle, "clGetPlatformIDs", clGetPlatformIDs, error_out) &&
        bind_fn(lib_handle, "clGetPlatformInfo", clGetPlatformInfo, error_out) &&
        bind_fn(lib_handle, "clGetDeviceIDs", clGetDeviceIDs, error_out) &&
        bind_fn(lib_handle, "clGetDeviceInfo", clGetDeviceInfo, error_out) &&
        bind_fn(lib_handle, "clCreateContext", clCreateContext, error_out) &&
        bind_fn(lib_handle, "clReleaseContext", clReleaseContext, error_out) &&
        bind_fn(lib_handle, "clCreateCommandQueue", clCreateCommandQueue, error_out) &&
        bind_fn(lib_handle, "clReleaseCommandQueue", clReleaseCommandQueue, error_out) &&
        bind_fn(lib_handle, "clFinish", clFinish, error_out) &&
        bind_fn(lib_handle, "clCreateBuffer", clCreateBuffer, error_out) &&
        bind_fn(lib_handle, "clReleaseMemObject", clReleaseMemObject, error_out) &&
        bind_fn(lib_handle, "clEnqueueReadBuffer", clEnqueueReadBuffer, error_out) &&
        bind_fn(lib_handle, "clEnqueueWriteBuffer", clEnqueueWriteBuffer, error_out) &&
        bind_fn(lib_handle, "clCreateProgramWithSource", clCreateProgramWithSource, error_out) &&
        bind_fn(lib_handle, "clBuildProgram", clBuildProgram, error_out) &&
        bind_fn(lib_handle, "clGetProgramBuildInfo", clGetProgramBuildInfo, error_out) &&
        bind_fn(lib_handle, "clReleaseProgram", clReleaseProgram, error_out) &&
        bind_fn(lib_handle, "clCreateKernel", clCreateKernel, error_out) &&
        bind_fn(lib_handle, "clSetKernelArg", clSetKernelArg, error_out) &&
        bind_fn(lib_handle, "clEnqueueNDRangeKernel", clEnqueueNDRangeKernel, error_out) &&
        bind_fn(lib_handle, "clReleaseKernel", clReleaseKernel, error_out);

    if (!ok) {
        unload();
        return false;
    }
    return true;
}

void OpenCLApi::unload() {
    if (!lib_handle) {
        return;
    }
#ifdef _WIN32
    FreeLibrary(static_cast<HMODULE>(lib_handle));
#else
    dlclose(lib_handle);
#endif
    lib_handle = nullptr;
}

std::string get_platform_string(const OpenCLApi& api, cl_platform_id platform, cl_platform_info param) {
    std::size_t size = 0;
    if (api.clGetPlatformInfo(platform, param, 0, nullptr, &size) != CL_SUCCESS || size == 0) {
        return "";
    }
    std::string buf(size, '\0');
    api.clGetPlatformInfo(platform, param, size, buf.data(), nullptr);
    while (!buf.empty() && buf.back() == '\0') {
        buf.pop_back();
    }
    return buf;
}

std::string get_device_string(const OpenCLApi& api, cl_device_id device, cl_device_info param) {
    std::size_t size = 0;
    if (api.clGetDeviceInfo(device, param, 0, nullptr, &size) != CL_SUCCESS || size == 0) {
        return "";
    }
    std::string buf(size, '\0');
    api.clGetDeviceInfo(device, param, size, buf.data(), nullptr);
    while (!buf.empty() && buf.back() == '\0') {
        buf.pop_back();
    }
    return buf;
}

}  // namespace sobel::gpu

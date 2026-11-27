cmake_minimum_required(VERSION 3.16)
project(sobel_dlp_framework LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

add_compile_options(-Wall -Wextra -Wpedantic)

include(CheckCXXCompilerFlag)

add_library(sobel_core STATIC
    src/image_rgba.cpp
    src/secuencial/sobel_sequential.cpp
)

target_include_directories(sobel_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_library(sobel_gpu_lib STATIC
    src/gpu/opencl_loader.cpp
    src/gpu/sobel_gpu.cpp
)

target_include_directories(sobel_gpu_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gpu
)

target_link_libraries(sobel_gpu_lib PUBLIC
    sobel_core
    $<$<NOT:$<BOOL:${WIN32}>>:dl>
)
add_library(sobel_simd_core STATIC
    src/simd/sobel_simd.cpp
)

target_link_libraries(sobel_simd_core PUBLIC sobel_core)

if(MSVC)
    target_compile_options(sobel_simd_core PRIVATE /arch:AVX2)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64|i.86")
    check_cxx_compiler_flag("-mavx2" COMPILER_SUPPORTS_AVX2)
    if(COMPILER_SUPPORTS_AVX2)
        target_compile_options(sobel_simd_core PRIVATE -mavx2)
    endif()
endif()

add_executable(sobel_seq
    src/secuencial/main.cpp
)

target_link_libraries(sobel_seq PRIVATE sobel_core)

add_executable(sobel_gpu
    src/gpu/main_gpu.cpp
)

target_link_libraries(sobel_gpu PRIVATE sobel_gpu_lib sobel_simd_core)
add_executable(sobel_simd
    src/simd/main_simd.cpp
)

target_link_libraries(sobel_simd PRIVATE sobel_simd_core)

enable_testing()

add_executable(test_sobel_sequential
    tests/test_sobel_sequential.cpp
)

target_link_libraries(test_sobel_sequential PRIVATE sobel_core)

add_executable(test_sobel_gpu
    tests/test_sobel_gpu.cpp
)

target_link_libraries(test_sobel_gpu PRIVATE sobel_gpu_lib)

add_test(NAME sobel_sequential_unit_tests COMMAND test_sobel_sequential)
add_test(NAME sobel_gpu_unit_tests COMMAND test_sobel_gpu)
add_executable(test_sobel_simd
    tests/test_sobel_simd.cpp
)

target_link_libraries(test_sobel_simd PRIVATE sobel_simd_core)

add_test(NAME sobel_simd_unit_tests COMMAND test_sobel_simd)

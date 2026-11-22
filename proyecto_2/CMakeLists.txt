cmake_minimum_required(VERSION 3.16)
project(sobel_dlp_framework LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

add_compile_options(-Wall -Wextra -Wpedantic)

add_library(sobel_core STATIC
    src/image_rgba.cpp
    src/sobel_sequential.cpp
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

add_executable(sobel_seq
    src/main.cpp
)

target_link_libraries(sobel_seq PRIVATE sobel_core)

add_executable(sobel_gpu
    src/gpu/main_gpu.cpp
)

target_link_libraries(sobel_gpu PRIVATE sobel_gpu_lib)

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

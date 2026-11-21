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
)

target_include_directories(sobel_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_executable(sobel_seq
    src/main.cpp
)

target_link_libraries(sobel_seq PRIVATE sobel_core)

#include "image_rgba.hpp"
#include "sobel_gpu.hpp"
#include "sobel_sequential.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <utility>

namespace {

void assert_exact_gpu_match(sobel::SobelGpuContext& ctx, const sobel::ImageRGBA& input,
                            const std::string& case_name) {
    const auto expected = sobel::sobel_sequential(input);
    sobel::ImageRGBA actual(input.width, input.height);
    sobel::GpuTimingResult timing;
    ctx.run(input, actual, &timing);
    const auto diff = sobel::compare_images(expected, actual);

    if (!diff.exact_match()) {
        std::cerr << "[FAIL] " << case_name
                  << " | max_diff=" << static_cast<int>(diff.max_abs_diff)
                  << " | mismatches=" << diff.mismatch_count
                  << " | mae=" << diff.mae << "\n";
        std::exit(1);
    }
}

void test_constant_image(sobel::SobelGpuContext& ctx) {
    const auto input = sobel::make_constant_image(16, 16, 100, 150, 200, 128);
    assert_exact_gpu_match(ctx, input, "constant_image_16x16");
}

void test_edges(sobel::SobelGpuContext& ctx) {
    assert_exact_gpu_match(ctx, sobel::make_vertical_edge_image(32, 32), "vertical_edge_32x32");
    assert_exact_gpu_match(ctx, sobel::make_horizontal_edge_image(32, 32), "horizontal_edge_32x32");
}

void test_impulse_and_corners(sobel::SobelGpuContext& ctx) {
    // Impulso central y en esquina (0,0) para probar zero padding en GPU.
    assert_exact_gpu_match(ctx, sobel::make_impulse_image(17, 17, 8, 8, 10, 20, 30), "impulse_center_17x17");
    assert_exact_gpu_match(ctx, sobel::make_impulse_image(17, 17, 0, 0, 10, 20, 30), "impulse_corner_17x17");
}

void test_isolated_channels(sobel::SobelGpuContext& ctx) {
    for (int c = 0; c < 3; ++c) {
        assert_exact_gpu_match(ctx, sobel::make_isolated_channel_image(24, 24, c, 120),
                               "isolated_channel_" + std::to_string(c));
    }
}

void test_non_multiple_dimensions(sobel::SobelGpuContext& ctx) {
    // Dimensiones no múltiplos de 16 (tamaño de work-group) para validar los límites del grid 2D.
    for (const auto [w, h] : {
             std::pair<int, int>{1, 1},
             std::pair<int, int>{7, 5},
             std::pair<int, int>{13, 9},
             std::pair<int, int>{33, 19},
         }) {
        const auto pattern = sobel::make_deterministic_pattern(w, h, 321);
        assert_exact_gpu_match(ctx, pattern, "non_multiple_" + std::to_string(w) + "x" + std::to_string(h));
    }
}

void test_persistent_context_and_separated_steps(sobel::SobelGpuContext& ctx) {
    // Valida reutilización de buffers y separación explícita H2D -> Kernel -> D2H sin recompilar.
    const auto input = sobel::make_deterministic_pattern(64, 48, 777);
    const auto expected = sobel::sobel_sequential(input);
    sobel::ImageRGBA output(64, 48);

    ctx.ensure_buffers(64, 48);
    for (int iter = 0; iter < 3; ++iter) {
        const double h2d_ms = ctx.upload(input);
        const double kernel_ms = ctx.execute_kernel(64, 48);
        const double d2h_ms = ctx.download(output);
        if (h2d_ms < 0.0 || kernel_ms < 0.0 || d2h_ms < 0.0) {
            std::cerr << "[FAIL] Tiempos de profiling negativos\n";
            std::exit(1);
        }

        const auto diff = sobel::compare_images(expected, output);
        if (!diff.exact_match()) {
            std::cerr << "[FAIL] persistent_steps_iter_" << iter
                      << " | max_diff=" << static_cast<int>(diff.max_abs_diff) << "\n";
            std::exit(1);
        }
    }
}

void test_convenience_api() {
    // Verifica compatibilidad con la API libre sobel_gpu() y sobel_gpu_into().
    const auto input = sobel::make_deterministic_pattern(64, 64, 99);
    const auto expected = sobel::sobel_sequential(input);
    const auto actual = sobel::sobel_gpu(input);
    const auto diff = sobel::compare_images(expected, actual);
    if (!diff.exact_match()) {
        std::cerr << "[FAIL] sobel_gpu_convenience | max_diff=" << static_cast<int>(diff.max_abs_diff) << "\n";
        std::exit(1);
    }
}

}  // namespace

int main() {
    try {
        sobel::SobelGpuContext ctx;
        test_constant_image(ctx);
        test_edges(ctx);
        test_impulse_and_corners(ctx);
        test_isolated_channels(ctx);
        test_non_multiple_dimensions(ctx);
        test_persistent_context_and_separated_steps(ctx);
        ctx.cleanup();

        test_convenience_api();
    } catch (const std::exception& ex) {
        std::cerr << "[ERROR] Excepcion en pruebas GPU: " << ex.what() << "\n";
        return 1;
    }

    std::cout << "[PASS] Todas las pruebas unitarias de sobel_gpu pasaron (7/7, exact_match=true).\n";
    return 0;
}

#include "image_rgba.hpp"
#include "sobel_sequential.hpp"
#include "sobel_simd.hpp"

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

void expect_exact_vs_sequential(const sobel::ImageRGBA& input, const std::string& label) {
    const auto expected = sobel::sobel_sequential(input);
    const auto actual = sobel::sobel_simd(input);
    const auto diff = sobel::compare_images(expected, actual);

    if (!diff.exact_match()) {
        std::ostringstream oss;
        oss << "[FAIL] " << label << " (" << input.width << "x" << input.height << "): "
            << "max_abs_diff=" << static_cast<int>(diff.max_abs_diff)
            << ", mismatch_count=" << diff.mismatch_count
            << ", mae=" << diff.mae;
        std::cerr << oss.str() << "\n";
        std::exit(1);
    }
}

void test_synthetic_cases() {
    expect_exact_vs_sequential(sobel::make_constant_image(32, 16, 100, 150, 200, 128), "constant_image");
    expect_exact_vs_sequential(sobel::make_vertical_edge_image(32, 16), "vertical_edge");
    expect_exact_vs_sequential(sobel::make_horizontal_edge_image(32, 16), "horizontal_edge");
    expect_exact_vs_sequential(sobel::make_impulse_image(15, 15, 7, 7, 10, 20, 30), "impulse_interior");
    expect_exact_vs_sequential(sobel::make_impulse_image(15, 15, 0, 0, 255, 128, 64), "impulse_corner");

    for (int c = 0; c < 3; ++c) {
        expect_exact_vs_sequential(
            sobel::make_isolated_channel_image(24, 12, c, 180),
            "isolated_channel_" + std::to_string(c));
    }
}

void test_boundary_and_tail_widths() {
    // Recorre anchos críticos alrededor del umbral del bucle AVX2 (12) y múltiplos de 8 + restos.
    const std::vector<std::pair<int, int>> dims = {
        {1, 1}, {2, 2}, {3, 3}, {7, 5}, {11, 4},
        {12, 3}, {13, 9}, {18, 6}, {19, 7}, {20, 5},
        {21, 5}, {31, 17}, {32, 16}, {33, 19}, {64, 64},
        {513, 257}
    };

    for (const auto& [w, h] : dims) {
        const auto pattern = sobel::make_deterministic_pattern(w, h, static_cast<uint32_t>(w * 31 + h));
        expect_exact_vs_sequential(pattern, "deterministic_pattern");

        // Verifica también sobel_simd_into sobre buffer prealocado.
        const auto expected = sobel::sobel_sequential(pattern);
        sobel::ImageRGBA actual_into(w, h);
        sobel::sobel_simd_into(pattern, actual_into);
        const auto diff = sobel::compare_images(expected, actual_into);
        if (!diff.exact_match()) {
            std::cerr << "[FAIL] sobel_simd_into mismatch en " << w << "x" << h << "\n";
            std::exit(1);
        }
    }
}

void test_benchmark_resolutions() {
    const auto img_512 = sobel::make_deterministic_pattern(512, 512, 42);
    expect_exact_vs_sequential(img_512, "512x512_seed42");
}

}  // namespace

int main() {
    test_synthetic_cases();
    test_boundary_and_tail_widths();
    test_benchmark_resolutions();

    std::cout << "[PASS] Todas las pruebas unitarias SIMD (" << sobel::sobel_simd_backend()
              << ") coinciden bit a bit con sobel_sequential.\n";
    return 0;
}

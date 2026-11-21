#include "image_rgba.hpp"
#include "sobel_sequential.hpp"

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>

namespace {

void expect_true(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << "\n";
        std::exit(1);
    }
}

void expect_eq(int actual, int expected, const std::string& label) {
    if (actual != expected) {
        std::ostringstream oss;
        oss << label << ": esperado=" << expected << ", obtenido=" << actual;
        expect_true(false, oss.str());
    }
}

void assert_alpha_255(const sobel::ImageRGBA& img, const std::string& case_name) {
    for (int y = 0; y < img.height; ++y) {
        for (int x = 0; x < img.width; ++x) {
            expect_eq(img.at(x, y, 3), 255, case_name + " alpha en (" + std::to_string(x) + "," + std::to_string(y) + ")");
        }
    }
}

void test_constant_image() {
    const auto input = sobel::make_constant_image(8, 8, 100, 150, 200, 128);
    const auto output = sobel::sobel_sequential(input);

    assert_alpha_255(output, "constant_image");

    // En el interior plano, todos los gradientes deben ser 0.
    for (int y = 1; y < 7; ++y) {
        for (int x = 1; x < 7; ++x) {
            for (int c = 0; c < 3; ++c) {
                expect_eq(output.at(x, y, c), 0, "constant interior");
            }
        }
    }

    // Por zero padding en la esquina (0,0), |Gx|+|Gy| = 6*V -> satura a 255.
    for (int c = 0; c < 3; ++c) {
        expect_eq(output.at(0, 0, c), 255, "constant corner zero-padding");
    }
}

void test_vertical_edge() {
    const auto input = sobel::make_vertical_edge_image(8, 8);
    const auto output = sobel::sobel_sequential(input);

    assert_alpha_255(output, "vertical_edge");

    // Zona plana izquierda y derecha (lejos de bordes exteriores y de x=4).
    expect_eq(output.at(1, 4, 0), 0, "vertical flat left");
    expect_eq(output.at(6, 4, 0), 0, "vertical flat right");

    // Transición en x=3 y x=4 debe saturar a 255 en los tres canales.
    for (int c = 0; c < 3; ++c) {
        expect_eq(output.at(3, 4, c), 255, "vertical edge left side");
        expect_eq(output.at(4, 4, c), 255, "vertical edge right side");
    }
}

void test_horizontal_edge() {
    const auto input = sobel::make_horizontal_edge_image(8, 8);
    const auto output = sobel::sobel_sequential(input);

    assert_alpha_255(output, "horizontal_edge");

    expect_eq(output.at(4, 1, 0), 0, "horizontal flat top");
    expect_eq(output.at(4, 6, 0), 0, "horizontal flat bottom");

    for (int c = 0; c < 3; ++c) {
        expect_eq(output.at(4, 3, c), 255, "horizontal edge top side");
        expect_eq(output.at(4, 4, c), 255, "horizontal edge bottom side");
    }
}

void test_impulse_response() {
    // Valores pequeños para evitar saturación y verificar pesos exactos de Kx y Ky.
    const auto input = sobel::make_impulse_image(5, 5, 2, 2, 10, 20, 30);
    const auto output = sobel::sobel_sequential(input);

    assert_alpha_255(output, "impulse");

    // El centro del impulso tiene peso 0 en Kx y Ky.
    for (int c = 0; c < 3; ++c) {
        expect_eq(output.at(2, 2, c), 0, "impulse center");
        expect_eq(output.at(0, 0, c), 0, "impulse out of 3x3 window");
    }

    // Vecinos ortogonales: peso 2 en un eje y 0 en el otro -> |Gx|+|Gy| = 2 * valor.
    expect_eq(output.at(2, 1, 0), 20, "impulse top R");
    expect_eq(output.at(2, 1, 1), 40, "impulse top G");
    expect_eq(output.at(2, 1, 2), 60, "impulse top B");
    expect_eq(output.at(1, 2, 0), 20, "impulse left R");

    // Vecinos diagonales: peso 1 en Kx y 1 en Ky -> |Gx|+|Gy| = 2 * valor.
    expect_eq(output.at(1, 1, 0), 20, "impulse diag R");
    expect_eq(output.at(3, 3, 1), 40, "impulse diag G");
    expect_eq(output.at(3, 1, 2), 60, "impulse diag B");
}

void test_isolated_channels() {
    for (int active = 0; active < 3; ++active) {
        const auto input = sobel::make_isolated_channel_image(8, 8, active, 120);
        const auto output = sobel::sobel_sequential(input);

        assert_alpha_255(output, "isolated_channel");

        for (int y = 0; y < output.height; ++y) {
            for (int x = 0; x < output.width; ++x) {
                for (int c = 0; c < 3; ++c) {
                    if (c != active) {
                        expect_eq(output.at(x, y, c), 0, "inactive channel must stay 0");
                    }
                }
            }
        }
        // El canal activo sí debe detectar el borde vertical en (4,4).
        expect_eq(output.at(4, 4, active), 255, "active channel edge");
    }
}

void test_non_multiple_and_tiny_dimensions() {
    // 1x1: todos los vecinos son zero padding y el centro tiene peso 0.
    const auto tiny = sobel::make_constant_image(1, 1, 50, 60, 70, 99);
    const auto tiny_out = sobel::sobel_sequential(tiny);
    expect_eq(tiny_out.at(0, 0, 0), 0, "1x1 R");
    expect_eq(tiny_out.at(0, 0, 1), 0, "1x1 G");
    expect_eq(tiny_out.at(0, 0, 2), 0, "1x1 B");
    expect_eq(tiny_out.at(0, 0, 3), 255, "1x1 A");

    // Dimensiones no múltiplos de 8 (7x5 y 13x9): verificar sobel_sequential vs sobel_sequential_into.
    for (const auto [w, h] : {std::pair<int, int>{7, 5}, std::pair<int, int>{13, 9}}) {
        const auto pattern = sobel::make_deterministic_pattern(w, h, 123);
        const auto out_alloc = sobel::sobel_sequential(pattern);
        sobel::ImageRGBA out_pre(w, h);
        sobel::sobel_sequential_into(pattern, out_pre);

        assert_alpha_255(out_alloc, "non_multiple");
        const auto diff = sobel::compare_images(out_alloc, out_pre);
        expect_true(diff.exact_match(), "sobel_sequential and sobel_sequential_into must match bit-for-bit");
    }
}

}  // namespace

int main() {
    test_constant_image();
    test_vertical_edge();
    test_horizontal_edge();
    test_impulse_response();
    test_isolated_channels();
    test_non_multiple_and_tiny_dimensions();

    std::cout << "[PASS] Todas las pruebas unitarias de sobel_sequential pasaron (6/6).\n";
    return 0;
}

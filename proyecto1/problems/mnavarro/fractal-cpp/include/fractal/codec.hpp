#pragma once
#include <cstdint>
#include <vector>

namespace fractal {
// Intensidades normalizadas [0,1], en orden por filas.
struct Image {
    int width = 0, height = 0;
    std::vector<float> pixels;
    float& at(int x, int y) { return pixels[static_cast<std::size_t>(y)*width+x]; }
    float at(int x, int y) const { return pixels[static_cast<std::size_t>(y)*width+x]; }
};
struct Config { int range = 8; int stride = 8; }; // domain = 2*range
struct Rule {
    std::uint32_t x = 0, y = 0;
    std::uint8_t symmetry = 0;
    float contrast = 0, brightness = 0;
};
struct Encoded {
    int width = 0, height = 0, range = 8;
    std::vector<Rule> rules; // Ranges en orden por filas.
};
void validate(const Encoded& code);
Encoded encode(const Image& image, Config config = {});
Image decode(const Encoded& code, int iterations = 40);
double mse(const Image& a, const Image& b);
}

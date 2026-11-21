#include "sobel_sequential.hpp"

#include <algorithm>
#include <cstdlib>
#include <stdexcept>

namespace sobel {

namespace {

// Máscaras de Sobel 3x3 fijadas en la especificación matemática del proyecto.
constexpr int kSobelX[3][3] = {
    {-1, 0, 1},
    {-2, 0, 2},
    {-1, 0, 1},
};

constexpr int kSobelY[3][3] = {
    {-1, -2, -1},
    { 0,  0,  0},
    { 1,  2,  1},
};

}  // namespace

void sobel_sequential_into(const ImageRGBA& input, ImageRGBA& output) {
    if (!input.is_valid()) {
        throw std::invalid_argument("Input image is not valid");
    }
    if (output.width != input.width || output.height != input.height || !output.is_valid()) {
        throw std::invalid_argument("Output image dimensions must match input image");
    }

    const int width = input.width;
    const int height = input.height;

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            int gx[3] = {0, 0, 0};
            int gy[3] = {0, 0, 0};

            // Convolución 3x3 con zero padding fuera de [0, width) x [0, height).
            for (int i = -1; i <= 1; ++i) {
                const int sy = y + i;
                if (sy < 0 || sy >= height) {
                    continue;
                }
                const int ky_row = i + 1;

                for (int j = -1; j <= 1; ++j) {
                    const int sx = x + j;
                    if (sx < 0 || sx >= width) {
                        continue;
                    }
                    const int kx_col = j + 1;
                    const int wx = kSobelX[ky_row][kx_col];
                    const int wy = kSobelY[ky_row][kx_col];

                    const std::size_t src_off = input.pixel_offset(sx, sy);
                    for (int c = 0; c < 3; ++c) {
                        const int sample = static_cast<int>(input.data[src_off + static_cast<std::size_t>(c)]);
                        gx[c] += sample * wx;
                        gy[c] += sample * wy;
                    }
                }
            }

            const std::size_t dst_off = output.pixel_offset(x, y);
            for (int c = 0; c < 3; ++c) {
                const int mag = std::abs(gx[c]) + std::abs(gy[c]);
                output.data[dst_off + static_cast<std::size_t>(c)] =
                    static_cast<uint8_t>(std::min(255, mag));
            }
            output.data[dst_off + 3] = 255;
        }
    }
}

ImageRGBA sobel_sequential(const ImageRGBA& input) {
    if (!input.is_valid()) {
        throw std::invalid_argument("Input image is not valid");
    }
    ImageRGBA output(input.width, input.height);
    sobel_sequential_into(input, output);
    return output;
}

}  // namespace sobel

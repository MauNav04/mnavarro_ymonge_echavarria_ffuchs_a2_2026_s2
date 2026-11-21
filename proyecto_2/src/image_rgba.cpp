#include "image_rgba.hpp"

#include <cmath>
#include <fstream>
#include <stdexcept>

namespace sobel {

namespace {

void validate_dims(int width, int height) {
    if (width <= 0 || height <= 0) {
        throw std::invalid_argument("Image dimensions must be positive");
    }
}

void set_pixel(ImageRGBA& img, int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
    const std::size_t off = img.pixel_offset(x, y);
    img.data[off + 0] = r;
    img.data[off + 1] = g;
    img.data[off + 2] = b;
    img.data[off + 3] = a;
}

}  // namespace

DiffMetrics compare_images(const ImageRGBA& expected, const ImageRGBA& actual) {
    if (!expected.is_valid() || !actual.is_valid()) {
        throw std::invalid_argument("Cannot compare invalid images");
    }
    if (expected.width != actual.width || expected.height != actual.height) {
        throw std::invalid_argument("Image dimensions do not match for comparison");
    }

    DiffMetrics metrics{};
    uint64_t abs_sum = 0;
    const std::size_t total_bytes = expected.byte_size();

    for (std::size_t i = 0; i < total_bytes; ++i) {
        const int diff = std::abs(static_cast<int>(expected.data[i]) - static_cast<int>(actual.data[i]));
        const uint8_t udiff = static_cast<uint8_t>(diff);
        if (udiff > metrics.max_abs_diff) {
            metrics.max_abs_diff = udiff;
        }
        if (diff != 0) {
            ++metrics.mismatch_count;
        }
        abs_sum += static_cast<uint64_t>(diff);
    }

    metrics.mae = static_cast<double>(abs_sum) / static_cast<double>(total_bytes);
    return metrics;
}

ImageRGBA make_constant_image(int width, int height, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    validate_dims(width, height);
    ImageRGBA img(width, height);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            set_pixel(img, x, y, r, g, b, a);
        }
    }
    return img;
}

ImageRGBA make_vertical_edge_image(int width, int height) {
    validate_dims(width, height);
    ImageRGBA img(width, height);
    const int split = width / 2;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const uint8_t v = (x < split) ? 0 : 255;
            set_pixel(img, x, y, v, v, v, 255);
        }
    }
    return img;
}

ImageRGBA make_horizontal_edge_image(int width, int height) {
    validate_dims(width, height);
    ImageRGBA img(width, height);
    const int split = height / 2;
    for (int y = 0; y < height; ++y) {
        const uint8_t v = (y < split) ? 0 : 255;
        for (int x = 0; x < width; ++x) {
            set_pixel(img, x, y, v, v, v, 255);
        }
    }
    return img;
}

ImageRGBA make_impulse_image(int width, int height, int ix, int iy, uint8_t r, uint8_t g, uint8_t b) {
    validate_dims(width, height);
    if (ix < 0 || ix >= width || iy < 0 || iy >= height) {
        throw std::out_of_range("Impulse coordinates out of image bounds");
    }
    ImageRGBA img = make_constant_image(width, height, 0, 0, 0, 255);
    set_pixel(img, ix, iy, r, g, b, 255);
    return img;
}

ImageRGBA make_isolated_channel_image(int width, int height, int channel, uint8_t value) {
    validate_dims(width, height);
    if (channel < 0 || channel > 2) {
        throw std::invalid_argument("Channel must be 0 (R), 1 (G), or 2 (B)");
    }
    ImageRGBA img = make_constant_image(width, height, 0, 0, 0, 255);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            // Alterna mitades para producir bordes claros únicamente en el canal seleccionado.
            if (x >= width / 2) {
                img.at(x, y, channel) = value;
            }
        }
    }
    return img;
}

ImageRGBA make_deterministic_pattern(int width, int height, uint32_t seed) {
    validate_dims(width, height);
    ImageRGBA img(width, height);
    uint32_t state = seed ? seed : 1U;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            // Xorshift32 determinista para poblar RGB sin depender de archivos externos.
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            const uint8_t r = static_cast<uint8_t>(state & 0xFFU);
            const uint8_t g = static_cast<uint8_t>((state >> 8) & 0xFFU);
            const uint8_t b = static_cast<uint8_t>((state >> 16) & 0xFFU);
            set_pixel(img, x, y, r, g, b, 255);
        }
    }
    return img;
}

bool save_ppm(const std::string& path, const ImageRGBA& img) {
    if (!img.is_valid()) {
        return false;
    }
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        return false;
    }
    out << "P6\n" << img.width << " " << img.height << "\n255\n";
    std::vector<uint8_t> rgb_row(static_cast<std::size_t>(img.width) * 3U);
    for (int y = 0; y < img.height; ++y) {
        for (int x = 0; x < img.width; ++x) {
            const std::size_t src = img.pixel_offset(x, y);
            const std::size_t dst = static_cast<std::size_t>(x) * 3U;
            rgb_row[dst + 0] = img.data[src + 0];
            rgb_row[dst + 1] = img.data[src + 1];
            rgb_row[dst + 2] = img.data[src + 2];
        }
        out.write(reinterpret_cast<const char*>(rgb_row.data()),
                  static_cast<std::streamsize>(rgb_row.size()));
    }
    return static_cast<bool>(out);
}

}  // namespace sobel

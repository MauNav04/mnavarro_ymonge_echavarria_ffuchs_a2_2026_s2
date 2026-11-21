#include "image_rgba.hpp"

#include <algorithm>
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

namespace {

uint32_t crc32_update(uint32_t crc, const uint8_t* data, std::size_t len) {
    crc = ~crc;
    for (std::size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b) {
            const uint32_t mask = static_cast<uint32_t>(-static_cast<int32_t>(crc & 1U));
            crc = (crc >> 1) ^ (0xEDB88320U & mask);
        }
    }
    return ~crc;
}

uint32_t adler32_compute(const uint8_t* data, std::size_t len) {
    uint32_t s1 = 1;
    uint32_t s2 = 0;
    constexpr uint32_t kMod = 65521U;
    for (std::size_t i = 0; i < len; ++i) {
        s1 = (s1 + data[i]) % kMod;
        s2 = (s2 + s1) % kMod;
    }
    return (s2 << 16) | s1;
}

void write_be32(std::vector<uint8_t>& buf, uint32_t v) {
    buf.push_back(static_cast<uint8_t>((v >> 24) & 0xFFU));
    buf.push_back(static_cast<uint8_t>((v >> 16) & 0xFFU));
    buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFFU));
    buf.push_back(static_cast<uint8_t>(v & 0xFFU));
}

void append_png_chunk(std::vector<uint8_t>& png, const char type[4],
                      const uint8_t* payload, std::size_t len) {
    write_be32(png, static_cast<uint32_t>(len));
    const std::size_t type_pos = png.size();
    png.insert(png.end(), type, type + 4);
    if (len > 0) {
        png.insert(png.end(), payload, payload + len);
    }
    const uint32_t crc = crc32_update(0, png.data() + type_pos, 4 + len);
    write_be32(png, crc);
}

}  // namespace

bool save_png(const std::string& path, const ImageRGBA& img) {
    if (!img.is_valid()) {
        return false;
    }

    const std::size_t row_bytes = static_cast<std::size_t>(img.width) * 4U;
    const std::size_t raw_size = static_cast<std::size_t>(img.height) * (1U + row_bytes);
    std::vector<uint8_t> raw(raw_size);

    for (int y = 0; y < img.height; ++y) {
        const std::size_t dst_row = static_cast<std::size_t>(y) * (1U + row_bytes);
        raw[dst_row] = 0;  // Filter type 0 (None)
        const std::size_t src_row = static_cast<std::size_t>(y) * row_bytes;
        std::copy(img.data.begin() + static_cast<std::ptrdiff_t>(src_row),
                  img.data.begin() + static_cast<std::ptrdiff_t>(src_row + row_bytes),
                  raw.begin() + static_cast<std::ptrdiff_t>(dst_row + 1U));
    }

    // Stream zlib con bloques DEFLATE sin comprimir (BTYPE=00).
    std::vector<uint8_t> zlib_stream;
    zlib_stream.reserve(raw_size + (raw_size / 65535U + 1U) * 5U + 6U);
    zlib_stream.push_back(0x78);
    zlib_stream.push_back(0x01);

    std::size_t offset = 0;
    while (offset < raw_size) {
        const std::size_t chunk = std::min<std::size_t>(65535U, raw_size - offset);
        const bool is_last = (offset + chunk) == raw_size;
        zlib_stream.push_back(is_last ? 0x01 : 0x00);
        const uint16_t len16 = static_cast<uint16_t>(chunk);
        const uint16_t nlen16 = static_cast<uint16_t>(~len16);
        zlib_stream.push_back(static_cast<uint8_t>(len16 & 0xFFU));
        zlib_stream.push_back(static_cast<uint8_t>((len16 >> 8) & 0xFFU));
        zlib_stream.push_back(static_cast<uint8_t>(nlen16 & 0xFFU));
        zlib_stream.push_back(static_cast<uint8_t>((nlen16 >> 8) & 0xFFU));
        zlib_stream.insert(zlib_stream.end(),
                           raw.begin() + static_cast<std::ptrdiff_t>(offset),
                           raw.begin() + static_cast<std::ptrdiff_t>(offset + chunk));
        offset += chunk;
    }
    write_be32(zlib_stream, adler32_compute(raw.data(), raw.size()));

    std::vector<uint8_t> png;
    static constexpr uint8_t kSig[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    png.insert(png.end(), kSig, kSig + 8);

    std::vector<uint8_t> ihdr;
    write_be32(ihdr, static_cast<uint32_t>(img.width));
    write_be32(ihdr, static_cast<uint32_t>(img.height));
    ihdr.push_back(8);  // bit depth
    ihdr.push_back(6);  // color type: RGBA
    ihdr.push_back(0);  // compression
    ihdr.push_back(0);  // filter
    ihdr.push_back(0);  // interlace
    append_png_chunk(png, "IHDR", ihdr.data(), ihdr.size());
    append_png_chunk(png, "IDAT", zlib_stream.data(), zlib_stream.size());
    append_png_chunk(png, "IEND", nullptr, 0);

    std::ofstream out(path, std::ios::binary);
    if (!out) {
        return false;
    }
    out.write(reinterpret_cast<const char*>(png.data()),
              static_cast<std::streamsize>(png.size()));
    return static_cast<bool>(out);
}

}  // namespace sobel

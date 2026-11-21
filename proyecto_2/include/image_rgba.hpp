#ifndef SOBEL_DLP_IMAGE_RGBA_HPP
#define SOBEL_DLP_IMAGE_RGBA_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace sobel {

/**
 * @brief Representa una imagen 2D en formato RGBA32 entrelazado (4 bytes por píxel, row-major).
 *
 * Cada píxel ocupa 4 bytes contiguos en orden [R, G, B, A]. Esta disposición coincide
 * con la especificación compartida entre la referencia secuencial y los aceleradores.
 */
struct ImageRGBA {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> data;

    ImageRGBA() = default;

    /**
     * @brief Construye una imagen con dimensiones dadas e inicializada en cero.
     */
    ImageRGBA(int w, int h)
        : width(w), height(h), data(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4U, 0) {}

    std::size_t pixel_count() const noexcept {
        return static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    }

    std::size_t byte_size() const noexcept {
        return data.size();
    }

    bool is_valid() const noexcept {
        return width > 0 && height > 0 &&
               data.size() == static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U;
    }

    std::size_t pixel_offset(int x, int y) const noexcept {
        return (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                static_cast<std::size_t>(x)) * 4U;
    }

    uint8_t& at(int x, int y, int channel) noexcept {
        return data[pixel_offset(x, y) + static_cast<std::size_t>(channel)];
    }

    const uint8_t& at(int x, int y, int channel) const noexcept {
        return data[pixel_offset(x, y) + static_cast<std::size_t>(channel)];
    }
};

/**
 * @brief Métricas de diferencia numérica entre la salida secuencial y una implementación paralela.
 */
struct DiffMetrics {
    uint8_t max_abs_diff = 0;
    double mae = 0.0;
    std::size_t mismatch_count = 0;

    bool exact_match() const noexcept {
        return max_abs_diff == 0 && mismatch_count == 0;
    }
};

/**
 * @brief Compara dos imágenes RGBA32 byte a byte y calcula máx |O_seq - O_par| y MAE.
 * @throws std::invalid_argument si las dimensiones o tamaños de buffer no coinciden.
 */
DiffMetrics compare_images(const ImageRGBA& expected, const ImageRGBA& actual);

/**
 * @brief Genera una imagen constante RGBA32.
 */
ImageRGBA make_constant_image(int width, int height, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);

/**
 * @brief Genera un borde vertical puro (mitad izquierda en 0, mitad derecha en 255).
 */
ImageRGBA make_vertical_edge_image(int width, int height);

/**
 * @brief Genera un borde horizontal puro (mitad superior en 0, mitad inferior en 255).
 */
ImageRGBA make_horizontal_edge_image(int width, int height);

/**
 * @brief Genera una imagen en negro con un único píxel impulso en (ix, iy).
 */
ImageRGBA make_impulse_image(int width, int height, int ix, int iy, uint8_t r, uint8_t g, uint8_t b);

/**
 * @brief Genera una imagen con actividad únicamente en un canal (0=R, 1=G, 2=B).
 */
ImageRGBA make_isolated_channel_image(int width, int height, int channel, uint8_t value);

/**
 * @brief Genera una imagen sintética determinista para pruebas de rendimiento y escalabilidad.
 */
ImageRGBA make_deterministic_pattern(int width, int height, uint32_t seed = 42);

/**
 * @brief Guarda los canales RGB de una imagen RGBA32 en formato PPM binario (P6) para inspección visual.
 * @return true si el archivo se escribió correctamente.
 */
bool save_ppm(const std::string& path, const ImageRGBA& img);

}  // namespace sobel

#endif  // SOBEL_DLP_IMAGE_RGBA_HPP

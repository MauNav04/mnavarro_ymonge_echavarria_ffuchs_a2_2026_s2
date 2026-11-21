#include "image_rgba.hpp"

#include <iostream>

/**
 * @brief Punto de entrada temporal para verificar ImageRGBA y utilidades de comparación.
 */
int main() {
    const auto img = sobel::make_deterministic_pattern(512, 512);
    const auto diff = sobel::compare_images(img, img);
    std::cout << "Proyecto 2 - ImageRGBA listo (" << img.width << "x" << img.height
              << ", bytes=" << img.byte_size()
              << ", exact_match=" << (diff.exact_match() ? "true" : "false") << ")\n";
    return 0;
}

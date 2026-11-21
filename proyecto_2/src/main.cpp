#include "image_rgba.hpp"
#include "sobel_sequential.hpp"

#include <iostream>

/**
 * @brief Punto de entrada temporal para verificar la ejecución de sobel_sequential.
 */
int main() {
    const auto input = sobel::make_vertical_edge_image(512, 512);
    const auto output = sobel::sobel_sequential(input);

    // En un borde vertical puro (0 | 255) en x = 256, el píxel interior (255, 256)
    // y (256, 256) deben saturar a 255 mientras el interior plano permanece en 0.
    std::cout << "Sobel secuencial ejecutado sobre 512x512\n"
              << "  Interior plano (100,100) R=" << static_cast<int>(output.at(100, 100, 0)) << "\n"
              << "  Borde vertical (256,256) R=" << static_cast<int>(output.at(256, 256, 0))
              << " A=" << static_cast<int>(output.at(256, 256, 3)) << "\n";
    return 0;
}

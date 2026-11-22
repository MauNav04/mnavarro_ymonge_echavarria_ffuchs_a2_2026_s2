#ifndef SOBEL_DLP_SOBEL_SIMD_HPP
#define SOBEL_DLP_SOBEL_SIMD_HPP

#include "image_rgba.hpp"

namespace sobel {

/**
 * @brief Aplica el operador Sobel 3x3 multicanal (R, G, B) usando instrucciones SIMD.
 *
 * En x86-64 con soporte AVX2 procesa 8 píxeles RGBA32 por iteración mediante registros
 * vectoriales de 256 bits, garantizando igualdad exacta bit a bit frente a sobel_sequential_into:
 *   G_{d,c}(x,y) = sum_{i=-1..1} sum_{j=-1..1} I(x+j, y+i, c) * K_d(i, j)
 *   O(x,y,c)     = min(255, |G_{x,c}(x,y)| + |G_{y,c}(x,y)|)
 *   O(x,y,A)     = 255
 *
 * @param input  Imagen de entrada válida en formato RGBA32.
 * @param output Buffer de salida prealocado con las mismas dimensiones que @p input.
 * @throws std::invalid_argument si @p input no es válida o las dimensiones no coinciden.
 */
void sobel_simd_into(const ImageRGBA& input, ImageRGBA& output);

/**
 * @brief Variante de conveniencia que reserva y retorna la imagen resultante.
 */
ImageRGBA sobel_simd(const ImageRGBA& input);

/**
 * @brief Retorna el nombre descriptivo del backend SIMD compilado ("AVX2", "NEON" o "Scalar").
 */
const char* sobel_simd_backend() noexcept;

}  // namespace sobel

#endif  // SOBEL_DLP_SOBEL_SIMD_HPP

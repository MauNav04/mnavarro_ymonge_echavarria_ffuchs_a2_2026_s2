#ifndef SOBEL_DLP_SOBEL_GPU_HPP
#define SOBEL_DLP_SOBEL_GPU_HPP

#include "image_rgba.hpp"

namespace sobel {

/**
 * @brief Aplica el operador Sobel 3x3 multicanal (R, G, B) ejecutando en GPU vía OpenCL.
 *
 * Respeta exactamente la misma especificación matemática entera y política de zero padding
 * que sobel_sequential_into, exigiendo igualdad bit a bit (max |O_seq - O_gpu| == 0).
 *
 * @param input  Imagen de entrada válida en formato RGBA32.
 * @param output Buffer de salida prealocado con las mismas dimensiones que @p input.
 * @throws std::invalid_argument si @p input o @p output no tienen dimensiones válidas/coincidentes.
 * @throws std::runtime_error si falla la inicialización o ejecución en el dispositivo OpenCL.
 */
void sobel_gpu_into(const ImageRGBA& input, ImageRGBA& output);

/**
 * @brief Variante de conveniencia que reserva y retorna la imagen resultante.
 */
ImageRGBA sobel_gpu(const ImageRGBA& input);

}  // namespace sobel

#endif  // SOBEL_DLP_SOBEL_GPU_HPP

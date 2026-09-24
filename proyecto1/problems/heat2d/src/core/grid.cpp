#include "grid.hpp"

#include <algorithm>
#include <cassert>
#include <stdexcept>

namespace heat2d {

/**
 * @brief Construye una malla rectangular y asigna una temperatura inicial.
 *
 * Se reserva un único bloque contiguo de rows * cols elementos.
 * Si alguna dimensión es cero, se lanza una excepción porque una
 * superficie vacía no tiene sentido para nuestra simulación.
 */
Grid::Grid(
    std::size_t rows,
    std::size_t cols,
    double initialTemperature
)
    : rows_(rows),
      cols_(cols),
      data_(rows * cols, initialTemperature) {

    if (rows == 0 || cols == 0) {
        throw std::invalid_argument(
            "Grid dimensions must be greater than zero."
        );
    }
}

/**
 * @brief Devuelve una referencia modificable a una celda determinada.
 *
 * Utiliza index() para transformar las coordenadas (fila, columna)
 * en la posición correspondiente dentro del vector interno.
 */
double& Grid::operator()(
    std::size_t row,
    std::size_t col
) {
    return data_[index(row, col)];
}

/**
 * @brief Devuelve una referencia de solo lectura a una celda determinada.
 *
 * Esta versión se utiliza cuando el objeto Grid es constante.
 */
const double& Grid::operator()(
    std::size_t row,
    std::size_t col
) const {
    return data_[index(row, col)];
}

/**
 * @brief Obtiene la cantidad de filas almacenadas.
 */
std::size_t Grid::rows() const {
    return rows_;
}

/**
 * @brief Obtiene la cantidad de columnas almacenadas.
 */
std::size_t Grid::cols() const {
    return cols_;
}

/**
 * @brief Calcula la cantidad total de celdas de la superficie.
 */
std::size_t Grid::size() const {
    return data_.size();
}

/**
 * @brief Asigna una misma temperatura a todas las celdas de la malla.
 */
void Grid::fill(double temperature) {
    std::fill(data_.begin(), data_.end(), temperature);
}

/**
 * @brief Convierte coordenadas bidimensionales en una posición lineal.
 *
 * Se utiliza almacenamiento por filas (row-major), por lo que todas
 * las columnas de una misma fila ocupan posiciones consecutivas.
 */
std::size_t Grid::index(
    std::size_t row,
    std::size_t col
) const {
    assert(row < rows_);
    assert(col < cols_);

    return row * cols_ + col;
}

}  // namespace heat2d
#include "grid.hpp"  // Incluye la declaración de la clase Grid que vamos a implementar.

#include <algorithm>  // Proporciona std::fill para asignar un valor a todas las celdas.
#include <cassert>    // Proporciona assert para verificar índices durante desarrollo.
#include <stdexcept>  // Proporciona std::invalid_argument para reportar dimensiones inválidas.

namespace heat2d {  // Todas las implementaciones pertenecen al namespace del proyecto Heat2D.

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
    : rows_(rows),                                  // Guarda la cantidad de filas recibida.
      cols_(cols),                                  // Guarda la cantidad de columnas recibida.
      data_(rows * cols, initialTemperature) {      // Crea todas las celdas con la temperatura inicial.

    if (rows == 0 || cols == 0) {                   // Comprueba que ninguna dimensión sea cero.

        throw std::invalid_argument(                // Lanza una excepción indicando el error.
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

    return data_[index(row, col)];  // Devuelve la celda correspondiente permitiendo modificarla.
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

    return data_[index(row, col)];  // Devuelve la celda sin permitir modificar su valor.
}

/**
 * @brief Obtiene la cantidad de filas almacenadas.
 */
std::size_t Grid::rows() const {

    return rows_;  // Retorna el número de filas de la malla.
}

/**
 * @brief Obtiene la cantidad de columnas almacenadas.
 */
std::size_t Grid::cols() const {

    return cols_;  // Retorna el número de columnas de la malla.
}

/**
 * @brief Calcula la cantidad total de celdas de la superficie.
 */
std::size_t Grid::size() const {

    return data_.size();  // Retorna la cantidad de elementos almacenados en el vector.
}

/**
 * @brief Asigna una misma temperatura a todas las celdas de la malla.
 */
void Grid::fill(double temperature) {

    std::fill(                   // Utiliza el algoritmo estándar para recorrer todo el vector.
        data_.begin(),           // Indica el inicio del rango que debe modificarse.
        data_.end(),             // Indica el final del rango que debe modificarse.
        temperature              // Especifica el nuevo valor que recibirá cada celda.
    );
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

    assert(row < rows_);          // Verifica durante desarrollo que la fila exista.

    assert(col < cols_);          // Verifica durante desarrollo que la columna exista.

    return row * cols_ + col;     // Convierte (fila, columna) en un índice lineal.
}

}  // namespace heat2d
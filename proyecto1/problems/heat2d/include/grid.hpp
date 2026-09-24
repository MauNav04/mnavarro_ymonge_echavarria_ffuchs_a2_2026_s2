#pragma once  // Evita que este archivo de cabecera sea incluido más de una vez durante la compilación.

#include <cstddef>  // Proporciona std::size_t, utilizado para tamaños e índices.
#include <vector>   // Proporciona std::vector, que almacenará las temperaturas de forma contigua.

namespace heat2d {  // Agrupa todas las estructuras del problema Heat Diffusion 2D dentro de un namespace propio.

/**
 * @brief Representa una superficie rectangular de temperaturas en dos dimensiones.
 *
 * La clase Grid permite trabajar conceptualmente con una matriz T[fila][columna],
 * pero almacena internamente todos los valores dentro de un único std::vector<double>.
 *
 * El almacenamiento contiguo facilita el acceso secuencial a memoria y será útil
 * posteriormente para estudiar localidad de caché y paralelización.
 */
class Grid {

public:

    /**
     * @brief Construye una nueva malla bidimensional.
     *
     * @param rows Cantidad de filas de la superficie.
     * @param cols Cantidad de columnas de la superficie.
     * @param initialTemperature Temperatura inicial asignada a todas las celdas.
     */
    Grid(
        std::size_t rows,
        std::size_t cols,
        double initialTemperature = 0.0
    );

    /**
     * @brief Permite acceder y modificar una celda de la malla.
     *
     * @param row Fila de la celda.
     * @param col Columna de la celda.
     * @return Referencia al valor de temperatura almacenado en la celda.
     */
    double& operator()(
        std::size_t row,
        std::size_t col
    );

    /**
     * @brief Permite consultar una celda de una malla constante.
     *
     * Esta versión evita modificar accidentalmente la temperatura cuando
     * el objeto Grid fue recibido como constante.
     *
     * @param row Fila de la celda.
     * @param col Columna de la celda.
     * @return Referencia constante al valor de temperatura almacenado.
     */
    const double& operator()(
        std::size_t row,
        std::size_t col
    ) const;

    /**
     * @brief Obtiene la cantidad de filas de la malla.
     *
     * @return Número de filas.
     */
    std::size_t rows() const;

    /**
     * @brief Obtiene la cantidad de columnas de la malla.
     *
     * @return Número de columnas.
     */
    std::size_t cols() const;

    /**
     * @brief Obtiene la cantidad total de celdas de la malla.
     *
     * @return Número total de elementos almacenados.
     */
    std::size_t size() const;

    /**
     * @brief Asigna la misma temperatura a todas las celdas.
     *
     * @param temperature Temperatura que se asignará a toda la superficie.
     */
    void fill(double temperature);

private:

    /**
     * @brief Convierte una posición bidimensional en un índice lineal.
     *
     * La conversión utiliza almacenamiento row-major:
     *
     * index = row * cols + col
     *
     * @param row Fila de la celda.
     * @param col Columna de la celda.
     * @return Índice correspondiente dentro del vector interno.
     */
    std::size_t index(
        std::size_t row,
        std::size_t col
    ) const;

    std::size_t rows_;       // Cantidad de filas existentes en la malla.

    std::size_t cols_;       // Cantidad de columnas existentes en la malla.

    std::vector<double> data_;  // Vector contiguo que almacena todas las temperaturas.
};

}  // namespace heat2d
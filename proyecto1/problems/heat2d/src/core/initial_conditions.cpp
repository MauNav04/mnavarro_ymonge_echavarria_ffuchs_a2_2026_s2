#include "initial_conditions.hpp"  // Incluye las declaraciones de las funciones que se implementan en este archivo.

#include <cmath>      // Proporciona std::sin para calcular la distribución sinusoidal.
#include <stdexcept>  // Proporciona std::invalid_argument para validar parámetros incorrectos.

namespace heat2d {  // Todas las implementaciones pertenecen al namespace Heat2D.

namespace {  // Crea un namespace interno para elementos utilizados solamente dentro de este archivo.

constexpr double PI = 3.14159265358979323846;  // Define el valor de pi utilizado en la distribución sinusoidal.

}  // namespace

/**
 * @brief Inicializa cada celda con una distribución sinusoidal bidimensional.
 *
 * Convierte los índices discretos de la malla en coordenadas físicas
 * (x,y) y utiliza esas coordenadas para evaluar la función de temperatura.
 */
void initializeSinusoidal(
    Grid& grid,
    double maxTemperature,
    double lengthX,
    double lengthY
) {

    if (lengthX <= 0.0 || lengthY <= 0.0) {  // Comprueba que ambas dimensiones físicas sean positivas.

        throw std::invalid_argument(  // Lanza una excepción si alguna longitud física es inválida.
            "Surface lengths must be greater than zero."
        );

    }

    if (grid.rows() < 2 || grid.cols() < 2) {  // Comprueba que existan suficientes puntos para representar ambos ejes.

        throw std::invalid_argument(  // Lanza una excepción si la malla no permite calcular espaciamiento.
            "Grid must contain at least two rows and two columns."
        );

    }

    const double dx = lengthX / static_cast<double>(grid.cols() - 1);  // Calcula la distancia física entre columnas consecutivas.

    const double dy = lengthY / static_cast<double>(grid.rows() - 1);  // Calcula la distancia física entre filas consecutivas.

    for (std::size_t row = 0; row < grid.rows(); ++row) {  // Recorre cada fila de la superficie.

        const double y = static_cast<double>(row) * dy;  // Convierte el índice de fila en una coordenada física Y.

        for (std::size_t col = 0; col < grid.cols(); ++col) {  // Recorre cada columna de la fila actual.

            const double x = static_cast<double>(col) * dx;  // Convierte el índice de columna en una coordenada física X.

            const double sinX = std::sin(PI * x / lengthX);  // Calcula la componente sinusoidal correspondiente al eje X.

            const double sinY = std::sin(PI * y / lengthY);  // Calcula la componente sinusoidal correspondiente al eje Y.

            grid(row, col) = maxTemperature * sinX * sinY;  // Calcula y almacena la temperatura inicial de la celda.

        }

    }

}

/**
 * @brief Asigna una temperatura fija a los cuatro bordes de la malla.
 *
 * Primero modifica los bordes superior e inferior y posteriormente
 * modifica los bordes izquierdo y derecho.
 */
void applyFixedBoundary(
    Grid& grid,
    double boundaryTemperature
) {

    for (std::size_t col = 0; col < grid.cols(); ++col) {  // Recorre todas las columnas de la superficie.

        grid(0, col) = boundaryTemperature;  // Asigna la temperatura fija al borde superior.

        grid(grid.rows() - 1, col) = boundaryTemperature;  // Asigna la temperatura fija al borde inferior.

    }

    for (std::size_t row = 0; row < grid.rows(); ++row) {  // Recorre todas las filas de la superficie.

        grid(row, 0) = boundaryTemperature;  // Asigna la temperatura fija al borde izquierdo.

        grid(row, grid.cols() - 1) = boundaryTemperature;  // Asigna la temperatura fija al borde derecho.

    }

}

}  // namespace heat2d
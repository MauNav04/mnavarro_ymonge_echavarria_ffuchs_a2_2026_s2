#include "initial_conditions.hpp"

#include <cmath>
#include <stdexcept>

namespace heat2d {

namespace {

constexpr double PI = 3.14159265358979323846;

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
    if (lengthX <= 0.0 || lengthY <= 0.0) {
        throw std::invalid_argument(
            "Surface lengths must be greater than zero."
        );
    }

    if (grid.rows() < 2 || grid.cols() < 2) {
        throw std::invalid_argument(
            "Grid must contain at least two rows and two columns."
        );
    }

    const double dx = lengthX / static_cast<double>(grid.cols() - 1);
    const double dy = lengthY / static_cast<double>(grid.rows() - 1);

    for (std::size_t row = 0; row < grid.rows(); ++row) {
        
        const double y = static_cast<double>(row) * dy;
        
        for (std::size_t col = 0; col < grid.cols(); ++col) {
            
            const double x = static_cast<double>(col) * dx;
            
            const double sinX = std::sin(PI * x / lengthX);
            const double sinY = std::sin(PI * y / lengthY);
            
            grid(row, col) = maxTemperature * sinX * sinY;
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
    for (std::size_t col = 0; col < grid.cols(); ++col) {
        grid(0, col) = boundaryTemperature;
        grid(grid.rows() - 1, col) = boundaryTemperature;
    }

    for (std::size_t row = 0; row < grid.rows(); ++row) {
        grid(row, 0) = boundaryTemperature;
        grid(row, grid.cols() - 1) = boundaryTemperature;
    }
}

}  // namespace heat2d
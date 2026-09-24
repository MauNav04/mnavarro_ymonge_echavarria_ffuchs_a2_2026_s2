#include "heat2d.hpp"
#include "initial_conditions.hpp"
#include <stdexcept>

namespace heat2d {

/**
 * @brief Comprueba la condición de estabilidad del esquema explícito 2D.
 *
 * Calcula el factor:
 *
 * alpha * dt * (1 / dx^2 + 1 / dy^2)
 *
 * y verifica que su valor sea menor o igual que 0.5.
 */
bool isStable(
    double alpha,
    double dt,
    double dx,
    double dy
) {
    if (alpha <= 0.0) {
        return false;
    }

    if (dt <= 0.0) {
        return false;
    }

    if (dx <= 0.0 || dy <= 0.0) {
        return false;
    }

    const double inverseDxSquared = 1.0 / (dx * dx);
    const double inverseDySquared = 1.0 / (dy * dy);
    const double stabilityFactor = alpha * dt * (inverseDxSquared + inverseDySquared);

    return stabilityFactor <= 0.5;
}

/**
 * @brief Calcula un paso temporal completo de difusión sobre la superficie.
 *
 * Cada celda interior se actualiza utilizando únicamente valores de la
 * malla current. Los resultados se escriben en next para evitar que una
 * actualización afecte los cálculos restantes de la misma iteración.
 */
void diffusionStep(
    const Grid& current,
    Grid& next,
    double alpha,
    double dt,
    double dx,
    double dy,
    double boundaryTemperature
) {
    if (current.rows() != next.rows() || current.cols() != next.cols()) {
        throw std::invalid_argument(
            "Current and next grids must have the same dimensions."
        );
    }

    if (current.rows() < 3 || current.cols() < 3) {
        throw std::invalid_argument(
            "Grid must contain at least three rows and three columns."
        );
    }

    if (alpha <= 0.0) {
        throw std::invalid_argument(
            "Thermal diffusivity alpha must be greater than zero."
        );
    }

    if (dt <= 0.0) {
        throw std::invalid_argument(
            "Time step dt must be greater than zero."
        );
    }

    if (dx <= 0.0 || dy <= 0.0) {
        throw std::invalid_argument(
            "Spatial steps dx and dy must be greater than zero."
        );
    }

    if (!isStable(alpha, dt, dx, dy)) {
        throw std::invalid_argument(
            "The selected alpha, dt, dx and dy values violate the stability condition."
        );
    }

    const double coefficientX = alpha * dt / (dx * dx);
    const double coefficientY = alpha * dt / (dy * dy);

    for (std::size_t row = 1; row < current.rows() - 1; ++row) {
        for (std::size_t col = 1; col < current.cols() - 1; ++col) {
            
            const double center = current(row, col);
            const double left   = current(row, col - 1);
            const double right  = current(row, col + 1);
            const double top    = current(row - 1, col);
            const double bottom = current(row + 1, col);

            const double diffusionX = coefficientX * (right - 2.0 * center + left);
            const double diffusionY = coefficientY * (bottom - 2.0 * center + top);

            next(row, col) = center + diffusionX + diffusionY;
        }
    }

    applyFixedBoundary(next, boundaryTemperature);
}

}  // namespace heat2d
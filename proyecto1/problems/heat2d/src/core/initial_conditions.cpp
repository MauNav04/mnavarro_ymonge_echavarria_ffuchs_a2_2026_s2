#include "initial_conditions.hpp"

#include <cmath>
#include <stdexcept>

namespace heat2d {

namespace {

constexpr double PI = 3.14159265358979323846;

}  // namespace

/**
 * @brief Initializes the grid with a two-dimensional sinusoidal temperature field.
 *
 * Discrete grid indices are mapped to physical coordinates using the domain
 * dimensions and evaluated with:
 *
 *     T(x, y) =
 *         maxTemperature *
 *         sin(pi * x / lengthX) *
 *         sin(pi * y / lengthY)
 *
 * This initial condition is smooth, satisfies zero-valued sinusoidal boundaries,
 * and admits a closed-form analytical solution for the heat equation. These
 * properties make it suitable for validating the numerical solver over time.
 *
 * @param grid Grid receiving the initial temperature field.
 * @param maxTemperature Peak temperature of the sinusoidal distribution.
 * @param lengthX Physical domain length along the X axis.
 * @param lengthY Physical domain length along the Y axis.
 *
 * @throws std::invalid_argument If either physical dimension is non-positive
 *         or if the grid has fewer than two rows or columns.
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

    const double dx =
        lengthX / static_cast<double>(grid.cols() - 1);

    const double dy =
        lengthY / static_cast<double>(grid.rows() - 1);

    for (std::size_t row = 0; row < grid.rows(); ++row) {
        const double y =
            static_cast<double>(row) * dy;

        for (std::size_t col = 0; col < grid.cols(); ++col) {
            const double x =
                static_cast<double>(col) * dx;

            const double sinX =
                std::sin(PI * x / lengthX);

            const double sinY =
                std::sin(PI * y / lengthY);

            grid(row, col) =
                maxTemperature * sinX * sinY;
        }
    }
}

/**
 * @brief Applies a fixed Dirichlet boundary condition to the grid.
 *
 * Sets the top, bottom, left, and right boundaries to the same prescribed
 * temperature. Interior cells are left unchanged.
 *
 * @param grid Grid whose boundary cells will be updated.
 * @param boundaryTemperature Fixed temperature assigned to the perimeter.
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
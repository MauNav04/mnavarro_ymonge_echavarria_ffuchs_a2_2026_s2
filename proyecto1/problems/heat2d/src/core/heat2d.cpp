#include "heat2d.hpp"

#include "initial_conditions.hpp"

#include <stdexcept>

namespace heat2d {

/**
 * @brief Checks the stability condition of the explicit finite-difference scheme.
 *
 * For the two-dimensional heat equation, the explicit Forward-Euler
 * discretization requires:
 *
 *     alpha * dt * (1 / dx^2 + 1 / dy^2) <= 1 / 2
 *
 * The condition relates the temporal step to the spatial resolution and
 * thermal diffusivity. Invalid or non-positive physical parameters are
 * considered unstable.
 *
 * @param alpha Thermal diffusivity.
 * @param dt Time-step size.
 * @param dx Spatial step along the X axis.
 * @param dy Spatial step along the Y axis.
 * @return true if the parameters satisfy the stability condition;
 *         false otherwise.
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

    const double inverseDxSquared =
        1.0 / (dx * dx);

    const double inverseDySquared =
        1.0 / (dy * dy);

    const double stabilityFactor =
        alpha * dt * (inverseDxSquared + inverseDySquared);

    return stabilityFactor <= 0.5;
}

/**
 * @brief Computes the constant coefficients used by the diffusion stencil.
 *
 * The coefficients
 *
 *     alpha * dt / dx^2
 *     alpha * dt / dy^2
 *
 * remain constant throughout a simulation. Computing them once avoids
 * repeating the same divisions for every cell update.
 *
 * @param alpha Thermal diffusivity.
 * @param dt Time-step size.
 * @param dx Spatial step along the X axis.
 * @param dy Spatial step along the Y axis.
 * @return Precomputed diffusion coefficients for both spatial directions.
 *
 * @throws std::invalid_argument If any parameter is non-positive or if the
 *         resulting discretization violates the stability condition.
 */
DiffusionCoefficients makeDiffusionCoefficients(
    double alpha,
    double dt,
    double dx,
    double dy
) {
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
            "The selected alpha, dt, dx and dy values violate "
            "the stability condition."
        );
    }

    return {
        alpha * dt / (dx * dx),
        alpha * dt / (dy * dy)
    };
}

/**
 * @brief Computes the next temperature of one interior grid cell.
 *
 * Applies the five-point finite-difference stencil using values exclusively
 * from `current` and writes the result to the corresponding cell in `next`.
 *
 * Because each invocation only reads the current state and writes one output
 * cell, multiple workers or logical contexts can safely process distinct
 * cells in parallel without synchronization inside this function.
 *
 * @param current Grid containing the current thermal state.
 * @param next Grid receiving the next thermal state.
 * @param row Interior row to update.
 * @param column Interior column to update.
 * @param coefficients Precomputed diffusion coefficients.
 */
void computeCell(
    const Grid& current,
    Grid& next,
    std::size_t row,
    std::size_t column,
    const DiffusionCoefficients& coefficients
) {
    const double center =
        current(row, column);

    const double left =
        current(row, column - 1);

    const double right =
        current(row, column + 1);

    const double top =
        current(row - 1, column);

    const double bottom =
        current(row + 1, column);

    const double diffusionX =
        coefficients.x *
        (right - 2.0 * center + left);

    const double diffusionY =
        coefficients.y *
        (bottom - 2.0 * center + top);

    next(row, column) =
        center + diffusionX + diffusionY;
}

/**
 * @brief Computes one complete sequential diffusion timestep.
 *
 * Interior cells are traversed in row-major order, matching the contiguous
 * storage layout of Grid and favoring spatial locality. Boundary cells are
 * excluded from the stencil traversal and restored afterward using the fixed
 * Dirichlet boundary condition.
 *
 * @param current Grid containing the current thermal state.
 * @param next Grid receiving the next thermal state.
 * @param alpha Thermal diffusivity.
 * @param dt Time-step size.
 * @param dx Spatial step along the X axis.
 * @param dy Spatial step along the Y axis.
 * @param boundaryTemperature Fixed temperature applied to the grid boundary.
 *
 * @throws std::invalid_argument If the grids have different dimensions,
 *         are too small for the five-point stencil, or if the numerical
 *         parameters are invalid or unstable.
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
    if (
        current.rows() != next.rows() ||
        current.cols() != next.cols()
    ) {
        throw std::invalid_argument(
            "Current and next grids must have the same dimensions."
        );
    }

    if (current.rows() < 3 || current.cols() < 3) {
        throw std::invalid_argument(
            "Grid must contain at least three rows and three columns."
        );
    }

    const DiffusionCoefficients coefficients =
        makeDiffusionCoefficients(
            alpha,
            dt,
            dx,
            dy
        );

    for (
        std::size_t row = 1;
        row < current.rows() - 1;
        ++row
    ) {
        for (
            std::size_t column = 1;
            column < current.cols() - 1;
            ++column
        ) {
            computeCell(
                current,
                next,
                row,
                column,
                coefficients
            );
        }
    }

    applyFixedBoundary(
        next,
        boundaryTemperature
    );
}

}  // namespace heat2d
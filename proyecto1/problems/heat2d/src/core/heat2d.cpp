#include "heat2d.hpp"

#include "initial_conditions.hpp"

#include <stdexcept>

namespace heat2d {

/**
 * @brief Evaluates the numerical stability of the Forward-Euler discretization.
 *
 * Implements the Courant–Friedrichs–Lewy (CFL) condition for the 2D Heat Equation.
 * The explicit finite-difference method will mathematically diverge (producing 
 * infinite or NaN values) if the time step `dt` is too large relative to the 
 * spatial grid resolution (`dx`, `dy`). 
 *
 * The stability criterion is:  alpha * dt * (1/dx^2 + 1/dy^2) <= 0.5
 *
 * @return true if the parameters guarantee a stable simulation.
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
 * @brief Precomputes the thermodynamic constants for the finite-difference stencil.
 *
 * Extracts the `(alpha * dt) / dx^2` and `(alpha * dt) / dy^2` terms. By calculating 
 * these once per simulation rather than inside the innermost `computeCell` loop, 
 * we eliminate millions of redundant floating-point multiplications and divisions, 
 * strictly optimizing for compute throughput.
 *
 * @throws std::invalid_argument If physical dimensions are invalid or numerically unstable.
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

    // Fail-fast on unstable parameters to prevent wasted compute cycles.
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
 * @brief Executes the 5-point finite-difference stencil for a single spatial cell.
 *
 * Approximates the second spatial derivatives (Laplacian) using the central, top, 
 * bottom, left, and right neighboring cells from the `current` state. 
 * The result is written directly to the `next` state buffer.
 *
 * This function must be lock-free and entirely stateless to guarantee thread safety 
 * when invoked concurrently by thousands of Fine/Coarse/CMP/SMT workers.
 */
void computeCell(
    const Grid& current,
    Grid& next,
    std::size_t row,
    std::size_t column,
    const DiffusionCoefficients& coefficients
) {
    // Read the current state of the 5-point cross stencil.
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

    // Calculate thermal gradient deltas over X and Y axes.
    const double diffusionX =
        coefficients.x *
        (right - 2.0 * center + left);

    const double diffusionY =
        coefficients.y *
        (bottom - 2.0 * center + top);

    // Apply the Forward-Euler temporal update.
    next(row, column) =
        center + diffusionX + diffusionY;
}

/**
 * @brief Helper function executing a complete sequential diffusion timestep.
 *
 * Integrates the spatial traversal and boundary condition enforcement into 
 * a single call. Internally iterates the grid in strict row-major order 
 * to guarantee optimal L1-D cache line utilization.
 *
 * @throws std::invalid_argument If the source and destination grids are structurally mismatched.
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

    // Iterate exclusively over the interior domain to avoid out-of-bounds neighbor access.
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

    // Reinforce Dirichlet boundary conditions (constant temperature borders).
    applyFixedBoundary(
        next,
        boundaryTemperature
    );
}

}  // namespace heat2d
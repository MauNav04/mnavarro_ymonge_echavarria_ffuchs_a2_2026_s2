#include "sequential.hpp"

#include "heat2d.hpp"
#include "initial_conditions.hpp"

#include <stdexcept>
#include <utility>

namespace heat2d {

namespace {

/**
 * @brief Validates the grid requirements shared by the sequential solver.
 *
 * Ensures that both the source and destination grids have identical dimensions
 * and are large enough to possess at least one interior cell (3x3 minimum). 
 * This is structurally required to evaluate the 5-point finite-difference stencil 
 * without reading out of bounds.
 *
 * @throws std::invalid_argument If the grids are mismatched or too small.
 */
void validateGrids(
    const Grid& current,
    const Grid& next
) {
    if (
        current.rows() != next.rows() ||
        current.cols() != next.cols()
    ) {
        throw std::invalid_argument(
            "Current and next grids must have the same dimensions."
        );
    }

    if (
        current.rows() < 3 ||
        current.cols() < 3
    ) {
        throw std::invalid_argument(
            "Grid must contain at least three rows and three columns."
        );
    }
}

}  // namespace

/**
 * @brief Single-threaded baseline implementation of the Heat 2D diffusion equation.
 * 
 * This function serves as the strictly serial benchmark ("ground truth") against 
 * which all concurrent models (FG, CG, CMP, SMT) are evaluated. It calculates 
 * the discrete timestep evolution using a lock-free, synchronization-free 
 * nested loop structure.
 * 
 * Performance characteristics:
 * - Extremely high IPC due to zero thread-synchronization overhead.
 * - Maximum spatial locality in L1-D cache because the inner loop iterates 
 *   strictly in row-major order (contiguous memory).
 */
void runSequential(
    Grid& current,
    Grid& next,
    std::size_t steps,
    double alpha,
    double dt,
    double dx,
    double dy,
    double boundaryTemperature
) {
    validateGrids(
        current,
        next
    );

    /*
     * Coefficients are invariant during the complete simulation.
     * Computing them once keeps the sequential execution path
     * comparable with the other execution models and prevents redundant
     * floating-point divisions deep inside the tight nested loops.
     */
    const DiffusionCoefficients coefficients =
        makeDiffusionCoefficients(
            alpha,
            dt,
            dx,
            dy
        );

    // Fast-path exit for zero-step simulations (no computation needed).
    if (steps == 0) {
        return;
    }

    // Main temporal progression loop.
    for (
        std::size_t step = 0;
        step < steps;
        ++step
    ) {
        /*
         * Spatial traversal over the strictly interior grid cells.
         * The outer loop traverses rows and the inner loop traverses columns.
         * This row-major access pattern prevents Cache Thrashing by ensuring
         * contiguous hardware prefetching.
         */
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

        // Enforce Dirichlet boundary conditions around the grid perimeter.
        applyFixedBoundary(
            next,
            boundaryTemperature
        );

        /*
         * Double-buffering pointer swap.
         * Promotes the newly calculated state to be the source for the next 
         * iteration, completely avoiding massive memory allocations or deep 
         * copies between timesteps.
         */
        std::swap(
            current,
            next
        );
    }
}

}  // namespace heat2d
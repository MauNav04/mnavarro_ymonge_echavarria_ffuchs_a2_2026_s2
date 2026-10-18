#include "sequential.hpp"

#include "heat2d.hpp"
#include "initial_conditions.hpp"

#include <stdexcept>
#include <utility>

namespace heat2d {

namespace {

/**
 * @brief Validates the grids used by the sequential solver.
 *
 * Both grids must have identical dimensions and contain at least one
 * interior cell so the five-point stencil can access all neighboring
 * positions safely.
 *
 * @param current Grid containing the current thermal state.
 * @param next Grid receiving the next thermal state.
 *
 * @throws std::invalid_argument If the grids have different dimensions
 *         or contain fewer than three rows or columns.
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
 * @brief Runs the sequential Heat2D diffusion simulation.
 *
 * This implementation provides the single-threaded reference used to validate
 * and benchmark the multithreaded execution models. Interior cells are processed
 * in row-major order using the same five-point stencil as the other models.
 *
 * @param current Grid containing the current thermal state.
 * @param next Grid used as the destination buffer for the next state.
 * @param steps Number of time steps to execute.
 * @param alpha Thermal diffusivity.
 * @param dt Time-step size.
 * @param dx Spatial step along the X axis.
 * @param dy Spatial step along the Y axis.
 * @param boundaryTemperature Fixed temperature applied to the grid boundary.
 *
 * @throws std::invalid_argument If the grid dimensions or numerical
 *         parameters are invalid.
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
     * Diffusion coefficients remain constant throughout the simulation.
     * Computing them once avoids repeating the same divisions for every
     * cell update and keeps the numerical formulation consistent across
     * execution models.
     */
    const DiffusionCoefficients coefficients =
        makeDiffusionCoefficients(
            alpha,
            dt,
            dx,
            dy
        );

    if (steps == 0) {
        return;
    }

    for (
        std::size_t step = 0;
        step < steps;
        ++step
    ) {
        /*
         * Traverse interior cells in row-major order so consecutive column
         * accesses correspond to contiguous locations in the Grid storage,
         * favoring spatial locality.
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

        applyFixedBoundary(
            next,
            boundaryTemperature
        );

        /*
         * Swap the roles of the two buffers so the newly computed state
         * becomes the source for the next timestep.
         */
        std::swap(
            current,
            next
        );
    }
}

}  // namespace heat2d
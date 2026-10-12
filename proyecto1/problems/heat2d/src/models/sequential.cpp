#include "sequential.hpp"

#include "heat2d.hpp"
#include "initial_conditions.hpp"

#include <stdexcept>
#include <utility>

namespace heat2d {

namespace {

/**
 * @brief Validates the grid requirements shared by the sequential solver.
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
     * comparable with the other execution models.
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

        std::swap(
            current,
            next
        );
    }
}

}  // namespace heat2d
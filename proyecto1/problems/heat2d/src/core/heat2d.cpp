#include "heat2d.hpp"

#include "initial_conditions.hpp"

#include <stdexcept>

namespace heat2d {

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

#pragma once

#include "grid.hpp"

#include <cstddef>

namespace heat2d {

/**
 * @brief Constant coefficients of the explicit diffusion scheme.
 */
struct DiffusionCoefficients {
    double x;
    double y;
};

/**
 * @brief Checks the numerical stability of the explicit 2D scheme.
 *
 * The following is required:
 *
 * alpha * dt * (1 / dx^2 + 1 / dy^2) <= 1 / 2
 */
[[nodiscard]]
bool isStable(
    double alpha,
    double dt,
    double dx,
    double dy
);

/**
 * @brief Validates the physical parameters and generates the coefficients
 *        used during a diffusion step.
 *
 * @throws std::invalid_argument if any parameter is invalid
 *         or if the configuration is numerically unstable.
 */
[[nodiscard]]
DiffusionCoefficients makeDiffusionCoefficients(
    double alpha,
    double dt,
    double dx,
    double dy
);

/**
 * @brief Computes the temperature of a single interior cell.
 *
 * Reads exclusively from current and writes only to next.
 * This function represents the minimum unit of work that will be used
 * by the Fine-Grained model.
 *
 * @param current Current thermal state.
 * @param next Next thermal state.
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
);

/**
 * @brief Computes a complete diffusion timestep.
 *
 * All interior cells are computed using current.
 * The results are stored in next.
 */
void diffusionStep(
    const Grid& current,
    Grid& next,
    double alpha,
    double dt,
    double dx,
    double dy,
    double boundaryTemperature
);

}  // namespace heat2d
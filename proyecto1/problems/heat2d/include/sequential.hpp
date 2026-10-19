#pragma once  // Prevents this header file from being included multiple times during compilation.

#include "grid.hpp"  // Includes the Grid definition used by the simulation.

#include <cstddef>  // Provides std::size_t to represent the number of time steps.

namespace heat2d {  // Groups the sequential implementation within the project namespace.

/**
 * @brief Runs the sequential 2D heat diffusion simulation.
 *
 * The function advances the simulation for a given number
 * of time steps. In each iteration the new state is computed
 * using diffusionStep() and then the current and next
 * grids are swapped.
 *
 * When the function finishes, current contains the final state of the
 * surface after executing all time steps.
 *
 * @param current Grid that initially contains the state of the surface.
 * @param next Auxiliary grid used to store the next state.
 * @param steps Number of time steps to be executed.
 * @param alpha Thermal diffusivity of the material.
 * @param dt Size of each time step.
 * @param dx Spatial separation between columns.
 * @param dy Spatial separation between rows.
 * @param boundaryTemperature Fixed temperature applied to the four boundaries.
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
);

}  // namespace heat2d
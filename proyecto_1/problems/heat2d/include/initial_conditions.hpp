#pragma once  // Prevents this header file from being included more than once during compilation.

#include "grid.hpp"  // Includes the Grid definition to be able to modify the temperature surface.

namespace heat2d {  // Groups these functions within the Heat2D project namespace.

/**
 * @brief Initializes the surface using a two-dimensional sinusoidal distribution.
 *
 * The initial temperature is computed as:
 *
 * T(x,y) = Tmax * sin(pi*x/Lx) * sin(pi*y/Ly)
 *
 * This distribution produces zero temperature at the boundaries and a
 * maximum temperature approximately at the center of the surface.
 *
 * @param grid Two-dimensional grid where the temperatures will be stored.
 * @param maxTemperature Maximum temperature of the initial distribution.
 * @param lengthX Physical length of the surface along the X axis.
 * @param lengthY Physical length of the surface along the Y axis.
 */
void initializeSinusoidal(
    Grid& grid,
    double maxTemperature,
    double lengthX,
    double lengthY
);

/**
 * @brief Sets a fixed temperature at the four boundaries of the surface.
 *
 * This function implements a Dirichlet boundary condition,
 * where the boundary temperature remains fixed at a known value.
 *
 * @param grid Two-dimensional grid that will be modified.
 * @param boundaryTemperature Temperature assigned to the four boundaries.
 */
void applyFixedBoundary(
    Grid& grid,
    double boundaryTemperature
);

}  // namespace heat2d
#include "initial_conditions.hpp"

#include <cmath>
#include <stdexcept>

namespace heat2d {

namespace {

// Constant definition for the mathematical formulation of the initial thermal wave.
constexpr double PI = 3.14159265358979323846;

}  // namespace

/**
 * @brief Initializes the computational grid with a 2D sinusoidal temperature distribution.
 *
 * Maps the discrete integer indices of the matrix to continuous physical coordinates (x, y)
 * based on the absolute spatial dimensions (lengthX, lengthY).
 *
 * It evaluates the function: T(x,y) = maxTemperature * sin(pi * x / Lx) * sin(pi * y / Ly).
 * 
 * A sinusoidal initial condition is deliberately chosen for diffusion experiments because 
 * it guarantees a smooth, continuous mathematical gradient. This prevents numerical shocks 
 * (which could trigger floating-point instability in explicit methods) and allows for 
 * straightforward analytical verification of the solver's accuracy over time.
 *
 * @param grid The grid to be populated with the initial thermal state.
 * @param maxTemperature The peak amplitude of the thermal wave at the exact geometric center.
 * @param lengthX The absolute physical width of the simulated domain (in meters).
 * @param lengthY The absolute physical height of the simulated domain (in meters).
 *
 * @throws std::invalid_argument If physical dimensions are non-positive or the grid 
 *         is too small to establish a spatial step.
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

    // Precalculate the physical distance between adjacent cells (dx, dy)
    const double dx = lengthX / static_cast<double>(grid.cols() - 1);
    const double dy = lengthY / static_cast<double>(grid.rows() - 1);

    // Row-major traversal to maximize L1-D cache write efficiency.
    for (std::size_t row = 0; row < grid.rows(); ++row) {
        
        const double y = static_cast<double>(row) * dy;
        
        for (std::size_t col = 0; col < grid.cols(); ++col) {
            
            const double x = static_cast<double>(col) * dx;
            
            const double sinX = std::sin(PI * x / lengthX);
            const double sinY = std::sin(PI * y / lengthY);
            
            grid(row, col) = maxTemperature * sinX * sinY;
        }
    }
}

/**
 * @brief Enforces Dirichlet boundary conditions around the grid perimeter.
 *
 * Overwrites the top, bottom, left, and right edges of the computational domain 
 * to a strictly constant temperature. In the context of the Heat Equation, this 
 * acts as an infinite heat sink (or source) connected to the edges, pulling the 
 * interior temperatures towards thermal equilibrium over time.
 *
 * @param grid The grid whose boundary cells will be clamped.
 * @param boundaryTemperature The constant scalar value applied to the perimeter.
 */
void applyFixedBoundary(
    Grid& grid,
    double boundaryTemperature
) {
    // Clamp the top (row 0) and bottom (last row) horizontal edges.
    // This access pattern is fully contiguous in memory.
    for (std::size_t col = 0; col < grid.cols(); ++col) {
        grid(0, col) = boundaryTemperature;
        grid(grid.rows() - 1, col) = boundaryTemperature;
    }

    // Clamp the left (col 0) and right (last col) vertical edges.
    // This access pattern involves strided memory jumps.
    for (std::size_t row = 0; row < grid.rows(); ++row) {
        grid(row, 0) = boundaryTemperature;
        grid(row, grid.cols() - 1) = boundaryTemperature;
    }
}

}  // namespace heat2d
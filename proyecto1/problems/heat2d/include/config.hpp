#pragma once  // Prevents this header file from being included more than once during compilation.

#include <cstddef>  // Provides std::size_t for grid sizes and step count.
#include <cmath>
namespace heat2d {  // Groups the configuration within the Heat2D project namespace.

/**
 * @brief Contains all the parameters needed to configure a Heat2D simulation.
 *
 * This structure centralizes the physical dimensions of the surface,
 * spatial resolution, thermal properties, initial conditions and
 * temporal parameters of the simulation.
 *
 * Keeping these parameters grouped avoids using scattered constants
 * and will make it easy to modify the experiments later on.
 */
struct SimulationConfig {

    double lengthX = 1.0;  // Physical length of the surface along the X axis, expressed in meters.

    double lengthY = 1.0;  // Physical length of the surface along the Y axis, expressed in meters.

    std::size_t rows = 801;  // Total number of discrete points used along the Y axis.

    std::size_t cols = 801;  // Total number of discrete points used along the X axis.

    double alpha = 0.05;  // Thermal diffusivity used by the diffusion model.

    double dt = 0.000005;  // Size of each time step, expressed in seconds (CFL stability).

    double totalTime = 0.005;  // Total physical time for 1000 steps (0.005 / 0.000005 = 1000).

    double maxTemperature = 100.0;  // Maximum temperature of the initial sinusoidal distribution.

    double boundaryTemperature = 0.0;  // Fixed temperature maintained at the four boundaries of the surface.

    /**
     * @brief Computes the physical spacing between consecutive columns.
     *
     * @return Distance dx between consecutive points along the X axis.
     */
    double dx() const {

        return lengthX / static_cast<double>(cols - 1);  // Divides the physical length by the actual number of intervals along X.
    }

    /**
     * @brief Computes the physical spacing between consecutive rows.
     *
     * @return Distance dy between consecutive points along the Y axis.
     */
    double dy() const {

        return lengthY / static_cast<double>(rows - 1);  // Divides the physical length by the actual number of intervals along Y.
    }

    [[nodiscard]]
    std::size_t steps() const {
        return static_cast<std::size_t>(
            std::llround(totalTime / dt)
        );
    }
};

}  // namespace heat2d
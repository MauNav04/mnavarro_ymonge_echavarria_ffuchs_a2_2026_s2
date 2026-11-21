#include "config.hpp"
#include "grid.hpp"
#include "heat2d.hpp"
#include "initial_conditions.hpp"
#include "sequential.hpp"

#include <exception>
#include <iomanip>
#include <iostream>

namespace {

/**
 * @brief Dumps the primary physical and experimental configurations to stdout.
 *
 * Outputs the spatial and temporal discretization parameters to guarantee 
 * reproducibility of the simulation state. This provides an immediate diagnostic 
 * overview before the compute-heavy phases begin.
 *
 * @param config Immutable configuration containing the domain dimensions,
 *               resolution, and temporal parameters of the experiment.
 */
void printConfiguration(
    const heat2d::SimulationConfig& config
) {
    std::cout << "\n=== Heat Diffusion 2D - Sequential Simulation ===\n";

    std::cout << std::fixed << std::setprecision(6);

    std::cout << "Surface size: "
              << config.lengthX
              << " m x "
              << config.lengthY
              << " m\n";

    std::cout << "Grid size: "
              << config.rows
              << " x "
              << config.cols
              << "\n";

    std::cout << "dx: "
              << config.dx()
              << " m\n";

    std::cout << "dy: "
              << config.dy()
              << " m\n";

    std::cout << "Alpha: "
              << config.alpha
              << "\n";

    std::cout << "dt: "
              << config.dt
              << " s\n";

    std::cout << "Requested simulation time: "
              << config.totalTime
              << " s\n";

    std::cout << "Time steps: "
              << config.steps()
              << "\n";

    std::cout << "Actually simulated time: "
              << static_cast<double>(config.steps()) * config.dt
              << " s\n";
}

/**
 * @brief Verifies and displays the numerical stability of the configuration.
 *
 * Evaluates the Forward-Euler stability criterion for the 2D Heat Equation 
 * (CFL condition) to ensure the simulation will not diverge mathematically 
 * before execution begins.
 *
 * @param config The simulation configuration to validate.
 */
void printStability(
    const heat2d::SimulationConfig& config
) {
    const bool stable = heat2d::isStable(
        config.alpha,
        config.dt,
        config.dx(),
        config.dy()
    );

    std::cout << "Numerical stability: "
              << (stable ? "STABLE" : "UNSTABLE")
              << "\n";
}

} // namespace   

/**
 * @brief Standalone executable entry point for the Sequential Heat2D model.
 * 
 * Serves as the single-threaded baseline ("ground truth") for the experimental 
 * framework. It orchestrates the memory allocation (double-buffering), applies 
 * the initial thermodynamic state, executes the non-concurrent discrete timestep 
 * evolution, and outputs the central temperature for validation.
 *
 * @return 0 on successful execution, 1 if a runtime exception is caught.
 */
int main() {
    try {
        const heat2d::SimulationConfig config;

        printConfiguration(config);

        printStability(config);

        /*
         * Double-buffering scheme allocation.
         * 'current' holds the state at timestep K, while 'next' receives 
         * the computed state for timestep K+1.
         */
        heat2d::Grid current(
            config.rows,
            config.cols,
            config.boundaryTemperature
        );

        heat2d::Grid next(
            config.rows,
            config.cols,
            config.boundaryTemperature
        );

        /*
         * Pre-heat the matrix according to the problem constraints.
         * The sinusoidal distribution provides a predictable gradient for 
         * mathematical validation across different execution models.
         */
        heat2d::initializeSinusoidal(
            current,
            config.maxTemperature,
            config.lengthX,
            config.lengthY
        );

        // Enforce Dirichlet boundary conditions (constant perimeter temperature).
        heat2d::applyFixedBoundary(
            current,
            config.boundaryTemperature
        );

        const std::size_t centerRow = config.rows / 2;
        const std::size_t centerCol = config.cols / 2;

        const double initialCenterTemperature = current(
            centerRow,
            centerCol
        );

        std::cout << "Initial center temperature: "
                  << initialCenterTemperature
                  << " C\n";

        /*
         * Execute the strictly serial computation kernel.
         */
        heat2d::runSequential(
            current,
            next,
            config.steps(),
            config.alpha,
            config.dt,
            config.dx(),
            config.dy(),
            config.boundaryTemperature
        );

        // Post-execution evaluation of the central node.
        const double finalCenterTemperature = current(
            centerRow,
            centerCol
        );

        std::cout << "Final center temperature: "
                  << finalCenterTemperature
                  << " C\n";

        std::cout << "Simulation completed successfully.\n\n";

        return 0;

    } catch (const std::exception& error) {
        std::cerr << "Simulation error: "
                  << error.what()
                  << "\n";

        return 1;
    }
}
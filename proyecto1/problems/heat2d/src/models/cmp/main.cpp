#include "cmp/cmp.hpp"

#include "config.hpp"
#include "grid.hpp"
#include "heat2d.hpp"
#include "initial_conditions.hpp"

#include <chrono>
#include <cstddef>
#include <exception>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

/**
 * @brief Parses the requested number of worker threads from CLI arguments.
 * 
 * Enforces a strictly positive integer value. Fast-fails on missing or 
 * non-numeric input to prevent undefined behavior during array allocation.
 *
 * @throws std::invalid_argument If the string is empty, negative, zero, or unparseable.
 */
std::size_t parseThreadCount(
    const char* argument
) {
    const std::string value(
        argument
    );

    if (
        value.empty() ||
        value.front() == '-'
    ) {
        throw std::invalid_argument(
            "Thread count must be a positive integer."
        );
    }

    std::size_t processedCharacters =
        0;

    const unsigned long long parsed =
        std::stoull(
            value,
            &processedCharacters
        );

    if (
        processedCharacters != value.size() ||
        parsed == 0
    ) {
        throw std::invalid_argument(
            "Thread count must be a positive integer."
        );
    }

    return static_cast<std::size_t>(
        parsed
    );
}


/**
 * @brief Dumps the primary physical and experimental configurations to stdout.
 *
 * Prints spatial/temporal discretization parameters, physical dimensions,
 * and the specific number of OS-level threads configured for this CMP run.
 */
void printConfiguration(
    const heat2d::SimulationConfig& config,
    std::size_t threadCount
) {
    std::cout
        << "\n=== Heat2D CMP Simulation ===\n\n";

    std::cout
        << std::fixed
        << std::setprecision(6);

    std::cout
        << "Surface size: "
        << config.lengthX
        << " m x "
        << config.lengthY
        << " m\n";

    std::cout
        << "Grid size: "
        << config.rows
        << " x "
        << config.cols
        << '\n';

    std::cout
        << "dx: "
        << config.dx()
        << " m\n";

    std::cout
        << "dy: "
        << config.dy()
        << " m\n";

    std::cout
        << "Alpha: "
        << config.alpha
        << '\n';

    std::cout
        << "dt: "
        << config.dt
        << " s\n";

    std::cout
        << "Requested simulation time: "
        << config.totalTime
        << " s\n";

    std::cout
        << "Time steps: "
        << config.steps()
        << '\n';

    std::cout
        << "CMP worker threads: "
        << threadCount
        << '\n';

    const bool stable =
        heat2d::isStable(
            config.alpha,
            config.dt,
            config.dx(),
            config.dy()
        );

    std::cout
        << "Numerical stability: "
        << (
            stable
                ? "STABLE"
                : "UNSTABLE"
        )
        << '\n';
}

}  // namespace


/**
 * @brief Standalone executable entry point for the CMP model.
 * 
 * 1. Parses configuration and asserts numerical stability.
 * 2. Initializes the double-buffered grid with a sinusoidal heat signature.
 * 3. Invokes the CMP thread pool to calculate the diffusion equation.
 * 4. Captures strictly the wall-clock execution time (excluding initialization).
 * 5. Dumps raw throughput and execution metrics to stdout.
 */
int main(
    int argc,
    char* argv[]
) {
    try {
        if (argc > 2) {
            throw std::invalid_argument(
                "Usage: ./heat2d_cmp [thread_count]"
            );
        }

        const std::size_t threadCount =
            argc >= 2
                ? parseThreadCount(
                    argv[1]
                )
                : 1;

        const heat2d::SimulationConfig config;

        printConfiguration(
            config,
            threadCount
        );

        // Pre-flight check: Forward Euler stability criterion.
        if (
            !heat2d::isStable(
                config.alpha,
                config.dt,
                config.dx(),
                config.dy()
            )
        ) {
            throw std::runtime_error(
                "Simulation configuration is numerically unstable."
            );
        }

        /*
         * Double-buffered grids.
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
         * Initial temperature distribution.
         */
        heat2d::initializeSinusoidal(
            current,
            config.maxTemperature,
            config.lengthX,
            config.lengthY
        );

        heat2d::applyFixedBoundary(
            current,
            config.boundaryTemperature
        );

        const std::size_t centerRow =
            config.rows / 2;

        const std::size_t centerColumn =
            config.cols / 2;

        const double initialCenterTemperature =
            current(
                centerRow,
                centerColumn
            );

        std::cout
            << "Initial center temperature: "
            << initialCenterTemperature
            << " C\n";

        /*
         * CMP configuration.
         */
        heat2d::CmpConfig cmpConfig;

        cmpConfig.threadCount =
            threadCount;

        /*
         * Measure the complete CMP kernel execution.
         * Note: Memory allocation and initialization are excluded from the chronometer
         * to measure strictly the compute throughput.
         */
        // === TIMING REGION START ===
        const auto start =
            std::chrono::steady_clock::now();

        const heat2d::CmpStats stats =
            heat2d::runCmp(
                current,
                next,
                config.steps(),
                config.alpha,
                config.dt,
                config.dx(),
                config.dy(),
                config.boundaryTemperature,
                cmpConfig
            );

        const auto end =
            std::chrono::steady_clock::now();
        // === TIMING REGION END ===

        const double elapsedSeconds =
            std::chrono::duration<double>(
                end - start
            ).count();

        const double finalCenterTemperature =
            current(
                centerRow,
                centerColumn
            );

        std::cout
            << "Final center temperature: "
            << finalCenterTemperature
            << " C\n";

        std::cout
            << "\n--- CMP Execution Statistics ---\n";

        std::cout
            << "Worker threads: "
            << stats.threadCount
            << '\n';

        std::cout
            << "Time steps: "
            << stats.timeSteps
            << '\n';

        std::cout
            << "Cell updates: "
            << stats.executedCellUpdates
            << '\n';

        std::cout
            << "Barrier phases: "
            << stats.barrierPhases
            << '\n';

        std::cout
            << "Execution time: "
            << elapsedSeconds
            << " s\n";

        if (elapsedSeconds > 0.0) {
            const double throughput =
                static_cast<double>(
                    stats.executedCellUpdates
                ) /
                elapsedSeconds;

            std::cout
                << "Throughput: "
                << throughput
                << " cell updates/s\n";
        }

        std::cout
            << "\nCMP simulation completed successfully.\n";

        return 0;

    } catch (const std::exception& error) {
        std::cerr
            << "\nCMP simulation failed.\n"
            << "Reason: "
            << error.what()
            << '\n';

        return 1;
    }
}
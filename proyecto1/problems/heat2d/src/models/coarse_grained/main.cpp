#include "coarse_grained/coarse_grained.hpp"

#include "config.hpp"
#include "grid.hpp"
#include "heat2d.hpp"
#include "initial_conditions.hpp"

#include <chrono>
#include <cstddef>
#include <exception>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {

constexpr std::size_t kDefaultRowsPerBlockingEvent =
    8;

/**
 * @brief Parses a command-line argument as a strictly positive integer.
 * 
 * Used primarily to extract the number of thread workers or the rows per chunk.
 * Enforces rigorous boundary checks to prevent runtime zero-division or 
 * overflow vulnerabilities.
 *
 * @throws std::invalid_argument If the string is empty, negative, zero, or unparseable.
 * @throws std::out_of_range If the numeric value exceeds `std::size_t` limits.
 */
std::size_t parsePositiveSize(
    const char* text,
    const char* argumentName
) {
    const std::string value(text);

    if (
        value.empty() ||
        value.front() == '-'
    ) {
        throw std::invalid_argument(
            std::string(argumentName) +
            " must be a positive integer."
        );
    }

    std::size_t consumed = 0;

    const unsigned long long parsed =
        std::stoull(
            value,
            &consumed
        );

    if (
        consumed != value.size() ||
        parsed == 0
    ) {
        throw std::invalid_argument(
            std::string(argumentName) +
            " must be a positive integer."
        );
    }

    if (
        parsed >
        static_cast<unsigned long long>(
            std::numeric_limits<std::size_t>::max()
        )
    ) {
        throw std::out_of_range(
            std::string(argumentName) +
            " is too large."
        );
    }

    return static_cast<std::size_t>(
        parsed
    );
}

/**
 * @brief Parses a command-line argument as a non-negative integer.
 *
 * Specifically designed for the blocking duration parameter, where `0` is a 
 * valid and expected input that explicitly deactivates the synthetic stall 
 * injection mechanism without breaking execution logic.
 */
std::size_t parseNonNegativeSize(
    const char* text,
    const char* argumentName
) {
    const std::string value(text);

    if (
        value.empty() ||
        value.front() == '-'
    ) {
        throw std::invalid_argument(
            std::string(argumentName) +
            " must be a non-negative integer."
        );
    }

    std::size_t consumed = 0;

    const unsigned long long parsed =
        std::stoull(
            value,
            &consumed
        );

    if (
        consumed != value.size()
    ) {
        throw std::invalid_argument(
            std::string(argumentName) +
            " must be a non-negative integer."
        );
    }

    if (
        parsed >
        static_cast<unsigned long long>(
            std::numeric_limits<std::size_t>::max()
        )
    ) {
        throw std::out_of_range(
            std::string(argumentName) +
            " is too large."
        );
    }

    return static_cast<std::size_t>(
        parsed
    );
}

/**
 * @brief Displays the Command Line Interface syntax and defaults.
 */
void printUsage(
    const char* programName
) {
    std::cerr
        << "Usage:\n"
        << "  "
        << programName
        << " <threads>"
        << " [blocking_microseconds]"
        << " [rows_per_blocking_event]\n\n"

        << "Defaults:\n"
        << "  blocking_microseconds     = 0\n"
        << "  rows_per_blocking_event   = 8\n\n"

        << "Examples:\n"
        << "  "
        << programName
        << " 4\n"

        << "  "
        << programName
        << " 4 0\n"

        << "  "
        << programName
        << " 4 100 8\n";
}

} // namespace

/**
 * @brief Entry point for the Coarse-Grained Multithreading (CGMT) standalone test.
 * 
 * Instantiates the computational grid, processes execution parameters from the CLI 
 * (including threads and latency injection values), and triggers the thread pool.
 * It is solely responsible for chronometer bounds (strictly around the computation 
 * phase) to avoid initialization/IO noise in the reported metrics.
 */
int main(
    int argc,
    char* argv[]
) {
    try {
        if (
            argc < 2 ||
            argc > 4
        ) {
            printUsage(
                argv[0]
            );

            return 1;
        }

        const std::size_t threadCount =
            parsePositiveSize(
                argv[1],
                "Thread count"
            );

        std::size_t blockingMicroseconds =
            0;

        std::size_t rowsPerBlockingEvent =
            kDefaultRowsPerBlockingEvent;

        // Parse optional latency parameters.
        if (argc >= 3) {
            blockingMicroseconds =
                parseNonNegativeSize(
                    argv[2],
                    "Blocking duration"
                );
        }

        if (argc >= 4) {
            rowsPerBlockingEvent =
                parsePositiveSize(
                    argv[3],
                    "Rows per blocking event"
                );
        }

        const heat2d::SimulationConfig config;

        // Assert numerical safety for the given Forward Euler discretization.
        const bool stable =
            heat2d::isStable(
                config.alpha,
                config.dt,
                config.dx(),
                config.dy()
            );

        if (!stable) {
            throw std::runtime_error(
                "The selected Heat2D configuration is unstable."
            );
        }

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

        // Pre-heat the matrix according to the problem constraints.
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

        /*
         * Setup the explicit struct defining Coarse-Grained properties.
         */
        heat2d::CoarseGrainedConfig coarseConfig;

        coarseConfig.threadCount =
            threadCount;

        coarseConfig.blockingDuration =
            std::chrono::microseconds{
                blockingMicroseconds
            };

        coarseConfig.rowsPerBlockingEvent =
            rowsPerBlockingEvent;

        // Configuration Dump
        std::cout
            << "\n=== Heat Diffusion 2D - Coarse-Grained ===\n";

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
            << "Time steps: "
            << config.steps()
            << '\n';

        std::cout
            << "Worker threads: "
            << coarseConfig.threadCount
            << '\n';

        std::cout
            << "Rows per blocking event: "
            << coarseConfig.rowsPerBlockingEvent
            << '\n';

        std::cout
            << "Blocking duration: "
            << blockingMicroseconds
            << " us\n";

        std::cout
            << "Synthetic blocking: "
            << (
                blockingMicroseconds > 0
                    ? "ENABLED"
                    : "DISABLED"
            )
            << '\n';

        std::cout
            << "Synchronization: reusable timestep barrier\n";

        std::cout
            << "Numerical stability: STABLE\n";

        // === TIMING REGION START ===
        const auto start =
            std::chrono::steady_clock::now();

        // Dispatch OS-level threads, evaluate chunks, and handle dual-phase sync.
        const heat2d::CoarseGrainedStats stats =
            heat2d::runCoarseGrained(
                current,
                next,
                config.steps(),
                config.alpha,
                config.dt,
                config.dx(),
                config.dy(),
                config.boundaryTemperature,
                coarseConfig
            );

        const auto end =
            std::chrono::steady_clock::now();
        // === TIMING REGION END ===

        const double elapsedSeconds =
            std::chrono::duration<double>(
                end - start
            ).count();

        // Post-execution evaluation.
        const double finalCenterTemperature =
            current(
                centerRow,
                centerColumn
            );

        std::cout
            << "\n--- Results ---\n";

        std::cout
            << "Initial center temperature: "
            << initialCenterTemperature
            << " C\n";

        std::cout
            << "Final center temperature: "
            << finalCenterTemperature
            << " C\n";

        std::cout
            << "Executed cell updates: "
            << stats.executedCellUpdates
            << '\n';

        std::cout
            << "Blocking events: "
            << stats.blockingEvents
            << '\n';

        std::cout
            << "Barrier phases: "
            << stats.barrierPhases
            << '\n';

        std::cout
            << "Execution time: "
            << elapsedSeconds
            << " s\n";

        std::cout
            << "Simulation completed successfully.\n\n";

        return 0;

    } catch (const std::exception& error) {
        std::cerr
            << "Coarse-Grained simulation error: "
            << error.what()
            << '\n';

        return 1;
    }
}
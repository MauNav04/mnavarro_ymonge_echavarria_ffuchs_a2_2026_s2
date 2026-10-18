#include "cmp/cmp.hpp"

#include "config.hpp"
#include "grid.hpp"
#include "initial_conditions.hpp"
#include "sequential.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

/**
 * @brief Stores numerical error metrics between two grid states.
 *
 * `l2Rms` represents the root-mean-square error over the complete grid,
 * while `lInfinity` stores the maximum absolute cell-wise difference.
 */
struct ErrorMetrics {
    double l2Rms{0.0};
    double lInfinity{0.0};
};


/**
 * @brief Creates the common initial state used by the stress test.
 *
 * Every sequential and CMP execution starts from an independent copy of
 * this same initialized grid, ensuring equivalent numerical conditions.
 *
 * @param config Simulation parameters used to construct the grid.
 * @return Initialized grid with the configured boundary condition applied.
 */
heat2d::Grid createInitialGrid(
    const heat2d::SimulationConfig& config
) {
    heat2d::Grid grid(
        config.rows,
        config.cols,
        config.boundaryTemperature
    );

    heat2d::initializeSinusoidal(
        grid,
        config.maxTemperature,
        config.lengthX,
        config.lengthY
    );

    heat2d::applyFixedBoundary(
        grid,
        config.boundaryTemperature
    );

    return grid;
}


/**
 * @brief Computes numerical differences between a reference and candidate grid.
 *
 * Calculates both the root-mean-square error and the maximum absolute
 * difference across all cells.
 *
 * @param reference Grid used as the numerical reference.
 * @param candidate Grid whose result is being validated.
 * @return Computed L2 RMS and L-infinity error metrics.
 *
 * @throws std::invalid_argument If the grids have different dimensions.
 */
ErrorMetrics calculateError(
    const heat2d::Grid& reference,
    const heat2d::Grid& candidate
) {
    if (
        reference.rows() != candidate.rows() ||
        reference.cols() != candidate.cols()
    ) {
        throw std::invalid_argument(
            "Cannot compare grids with different dimensions."
        );
    }

    double squaredErrorSum =
        0.0;

    double maximumError =
        0.0;

    for (
        std::size_t row = 0;
        row < reference.rows();
        ++row
    ) {
        for (
            std::size_t column = 0;
            column < reference.cols();
            ++column
        ) {
            const double difference =
                candidate(row, column) -
                reference(row, column);

            squaredErrorSum +=
                difference *
                difference;

            maximumError =
                std::max(
                    maximumError,
                    std::abs(
                        difference
                    )
                );
        }
    }

    ErrorMetrics error;

    error.l2Rms =
        std::sqrt(
            squaredErrorSum /
            static_cast<double>(
                reference.size()
            )
        );

    error.lInfinity =
        maximumError;

    return error;
}


/**
 * @brief Validates execution statistics reported by the CMP model.
 *
 * The test verifies that the solver performed the expected number of cell
 * updates and timesteps, used the requested number of workers, and completed
 * exactly two barrier phases per timestep.
 *
 * @param stats Statistics returned by the CMP execution.
 * @param config Simulation configuration used by the execution.
 * @param threadCount Expected number of CMP worker threads.
 *
 * @throws std::runtime_error If any reported statistic violates the expected
 *         execution invariants.
 */
void validateStats(
    const heat2d::CmpStats& stats,
    const heat2d::SimulationConfig& config,
    std::size_t threadCount
) {
    const std::uint64_t expectedUpdates =
        static_cast<std::uint64_t>(
            config.rows - 2
        ) *
        static_cast<std::uint64_t>(
            config.cols - 2
        ) *
        static_cast<std::uint64_t>(
            config.steps()
        );

    if (
        stats.executedCellUpdates !=
        expectedUpdates
    ) {
        throw std::runtime_error(
            "Unexpected CMP cell-update count."
        );
    }

    if (
        stats.threadCount !=
        threadCount
    ) {
        throw std::runtime_error(
            "Unexpected CMP worker-thread count."
        );
    }

    if (
        stats.timeSteps !=
        config.steps()
    ) {
        throw std::runtime_error(
            "Unexpected CMP timestep count."
        );
    }

    const std::uint64_t expectedBarrierPhases =
        static_cast<std::uint64_t>(
            config.steps()
        ) *
        2ULL;

    if (
        stats.barrierPhases !=
        expectedBarrierPhases
    ) {
        throw std::runtime_error(
            "Unexpected CMP barrier-phase count."
        );
    }
}

}  // namespace


/**
 * @brief Runs the CMP stress-validation suite.
 *
 * Executes the same large Heat2D workload with several CMP worker counts and
 * compares every result against a single sequential reference. The test checks
 * numerical equivalence, internal execution statistics, timing, throughput,
 * and observed speedup.
 *
 * @return 0 if every configuration passes; 1 if any validation fails.
 */
int main() {
    try {
        heat2d::SimulationConfig config;

        /*
         * Use the same large workload employed by the experimental pipeline
         * so the stress test exercises the production-scale configuration.
         */
        config.lengthX = 1.0;
        config.lengthY = 1.0;

        config.rows = 801;
        config.cols = 801;

        config.alpha = 0.05;
        config.dt = 0.000005;
        config.totalTime = 0.005;

        config.maxTemperature = 100.0;
        config.boundaryTemperature = 0.0;

        const std::size_t repetitions =
            3;

        const std::vector<std::size_t> threadCounts{
            1,
            2,
            4,
            8
        };

        std::cout
            << "\n=== Heat2D CMP Stress Test ===\n\n";

        std::cout
            << std::fixed
            << std::setprecision(9);

        std::cout
            << "Grid: "
            << config.rows
            << " x "
            << config.cols
            << '\n';

        std::cout
            << "Steps: "
            << config.steps()
            << '\n';

        std::cout
            << "dt: "
            << config.dt
            << " s\n";

        std::cout
            << "Repetitions per configuration: "
            << repetitions
            << '\n';

        const std::uint64_t expectedUpdates =
            static_cast<std::uint64_t>(
                config.rows - 2
            ) *
            static_cast<std::uint64_t>(
                config.cols - 2
            ) *
            static_cast<std::uint64_t>(
                config.steps()
            );

        std::cout
            << "Expected cell updates: "
            << expectedUpdates
            << '\n';

        /*
         * Build one common initial state and copy it into each execution so
         * all configurations begin from identical numerical conditions.
         */
        const heat2d::Grid initial =
            createInitialGrid(
                config
            );

        /*
         * Compute the sequential reference once. Every CMP result is compared
         * against this final grid using the same physical and numerical setup.
         */
        heat2d::Grid sequentialCurrent =
            initial;

        heat2d::Grid sequentialNext(
            config.rows,
            config.cols,
            config.boundaryTemperature
        );

        std::cout
            << "\nRunning sequential reference...\n";

        const auto sequentialStart =
            std::chrono::steady_clock::now();

        heat2d::runSequential(
            sequentialCurrent,
            sequentialNext,
            config.steps(),
            config.alpha,
            config.dt,
            config.dx(),
            config.dy(),
            config.boundaryTemperature
        );

        const auto sequentialEnd =
            std::chrono::steady_clock::now();

        const double sequentialSeconds =
            std::chrono::duration<double>(
                sequentialEnd -
                sequentialStart
            ).count();

        std::cout
            << "Sequential reference time: "
            << sequentialSeconds
            << " s\n";

        /*
         * Exercise multiple CMP worker counts while preserving the same
         * workload and initial state for every repetition.
         */
        for (
            const std::size_t threadCount :
            threadCounts
        ) {
            std::cout
                << "\n--- CMP threads = "
                << threadCount
                << " ---\n";

            for (
                std::size_t repetition = 1;
                repetition <= repetitions;
                ++repetition
            ) {
                heat2d::Grid cmpCurrent =
                    initial;

                heat2d::Grid cmpNext(
                    config.rows,
                    config.cols,
                    config.boundaryTemperature
                );

                heat2d::CmpConfig cmpConfig;

                cmpConfig.threadCount =
                    threadCount;

                const auto start =
                    std::chrono::steady_clock::now();

                const heat2d::CmpStats stats =
                    heat2d::runCmp(
                        cmpCurrent,
                        cmpNext,
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

                const double elapsedSeconds =
                    std::chrono::duration<double>(
                        end -
                        start
                    ).count();

                validateStats(
                    stats,
                    config,
                    threadCount
                );

                const ErrorMetrics error =
                    calculateError(
                        sequentialCurrent,
                        cmpCurrent
                    );

                constexpr double tolerance =
                    1e-12;

                if (
                    error.lInfinity >
                    tolerance
                ) {
                    throw std::runtime_error(
                        "CMP stress result differs from Sequential."
                    );
                }

                const double throughput =
                    static_cast<double>(
                        stats.executedCellUpdates
                    ) /
                    elapsedSeconds;

                const double speedup =
                    sequentialSeconds /
                    elapsedSeconds;

                std::cout
                    << "Run "
                    << repetition
                    << ": time = "
                    << elapsedSeconds
                    << " s"
                    << ", speedup = "
                    << speedup
                    << "x"
                    << ", throughput = "
                    << throughput
                    << " updates/s"
                    << ", L2 = "
                    << error.l2Rms
                    << ", Linf = "
                    << error.lInfinity
                    << '\n';
            }
        }

        std::cout
            << "\nCMP stress test: PASSED\n";

        return 0;

    } catch (
        const std::exception& error
    ) {
        std::cerr
            << "\nCMP stress test: FAILED\n"
            << "Reason: "
            << error.what()
            << '\n';

        return 1;
    }
}
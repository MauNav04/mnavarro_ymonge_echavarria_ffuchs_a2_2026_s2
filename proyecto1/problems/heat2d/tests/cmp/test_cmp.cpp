#include "cmp/cmp.hpp"

#include "config.hpp"
#include "grid.hpp"
#include "initial_conditions.hpp"
#include "sequential.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

/**
 * @brief Returns the maximum absolute difference between two grids.
 */
double maximumGridDifference(
    const heat2d::Grid& first,
    const heat2d::Grid& second
) {
    if (
        first.rows() != second.rows() ||
        first.cols() != second.cols()
    ) {
        throw std::invalid_argument(
            "Cannot compare grids with different dimensions."
        );
    }

    double maximumDifference =
        0.0;

    for (
        std::size_t row = 0;
        row < first.rows();
        ++row
    ) {
        for (
            std::size_t column = 0;
            column < first.cols();
            ++column
        ) {
            maximumDifference =
                std::max(
                    maximumDifference,
                    std::abs(
                        first(row, column) -
                        second(row, column)
                    )
                );
        }
    }

    return maximumDifference;
}


/**
 * @brief Creates the common initial Heat2D state used by the tests.
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
 * @brief Verifies one CMP configuration against the sequential model.
 */
void testAgainstSequential(
    std::size_t threadCount
) {
    heat2d::SimulationConfig config;

    /*
     * Moderate grid:
     *
     * Large enough to exercise row partitioning while keeping
     * the integration test fast.
     */
    config.rows = 51;
    config.cols = 51;

    config.alpha = 0.05;
    config.dt = 0.0001;
    config.totalTime = 0.01;

    const std::size_t steps =
        config.steps();

    const heat2d::Grid initial =
        createInitialGrid(
            config
        );

    /*
     * Sequential reference.
     */
    heat2d::Grid sequentialCurrent =
        initial;

    heat2d::Grid sequentialNext(
        config.rows,
        config.cols,
        config.boundaryTemperature
    );

    heat2d::runSequential(
        sequentialCurrent,
        sequentialNext,
        steps,
        config.alpha,
        config.dt,
        config.dx(),
        config.dy(),
        config.boundaryTemperature
    );

    /*
     * CMP execution.
     */
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

    const heat2d::CmpStats stats =
        heat2d::runCmp(
            cmpCurrent,
            cmpNext,
            steps,
            config.alpha,
            config.dt,
            config.dx(),
            config.dy(),
            config.boundaryTemperature,
            cmpConfig
        );

    /*
     * The result should be numerically identical because both
     * execution models evaluate the same finite-difference kernel.
     */
    const double maximumError =
        maximumGridDifference(
            sequentialCurrent,
            cmpCurrent
        );

    constexpr double tolerance =
        1e-12;

    if (
        maximumError >
        tolerance
    ) {
        throw std::runtime_error(
            "CMP result differs from Sequential."
        );
    }

    /*
     * Validate statistics.
     */
    const std::uint64_t expectedUpdates =
        static_cast<std::uint64_t>(
            config.rows - 2
        ) *
        static_cast<std::uint64_t>(
            config.cols - 2
        ) *
        static_cast<std::uint64_t>(
            steps
        );

    if (
        stats.executedCellUpdates !=
        expectedUpdates
    ) {
        throw std::runtime_error(
            "Unexpected number of CMP cell updates."
        );
    }

    if (
        stats.threadCount !=
        threadCount
    ) {
        throw std::runtime_error(
            "Incorrect CMP worker-thread count."
        );
    }

    if (
        stats.timeSteps !=
        steps
    ) {
        throw std::runtime_error(
            "Incorrect CMP timestep count."
        );
    }

    const std::uint64_t expectedBarrierPhases =
        static_cast<std::uint64_t>(
            steps
        ) *
        2ULL;

    if (
        stats.barrierPhases !=
        expectedBarrierPhases
    ) {
        throw std::runtime_error(
            "Unexpected number of CMP barrier phases."
        );
    }

    std::cout
        << "  [PASS] threads = "
        << threadCount
        << ", updates = "
        << stats.executedCellUpdates
        << ", barrier phases = "
        << stats.barrierPhases
        << ", max error = "
        << maximumError
        << '\n';
}


/**
 * @brief CMP must reject a configuration with zero workers.
 */
void testRejectsZeroThreads() {
    heat2d::SimulationConfig config;

    heat2d::Grid current =
        createInitialGrid(
            config
        );

    heat2d::Grid next(
        config.rows,
        config.cols,
        config.boundaryTemperature
    );

    heat2d::CmpConfig cmpConfig;

    cmpConfig.threadCount = 0;

    bool rejected =
        false;

    try {
        const auto stats =
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

        (void)stats;

    } catch (
        const std::invalid_argument&
    ) {
        rejected = true;
    }

    if (!rejected) {
        throw std::runtime_error(
            "CMP accepted zero worker threads."
        );
    }

    std::cout
        << "  [PASS] zero worker threads rejected\n";
}


/**
 * @brief Zero timesteps must leave the grid unchanged.
 */
void testZeroSteps() {
    heat2d::SimulationConfig config;

    const heat2d::Grid initial =
        createInitialGrid(
            config
        );

    heat2d::Grid current =
        initial;

    heat2d::Grid next(
        config.rows,
        config.cols,
        config.boundaryTemperature
    );

    heat2d::CmpConfig cmpConfig;

    cmpConfig.threadCount = 4;

    const heat2d::CmpStats stats =
        heat2d::runCmp(
            current,
            next,
            0,
            config.alpha,
            config.dt,
            config.dx(),
            config.dy(),
            config.boundaryTemperature,
            cmpConfig
        );

    if (
        stats.executedCellUpdates !=
        0
    ) {
        throw std::runtime_error(
            "Zero-step CMP execution reported cell updates."
        );
    }

    if (
        stats.barrierPhases !=
        0
    ) {
        throw std::runtime_error(
            "Zero-step CMP execution reported barrier phases."
        );
    }

    const double maximumError =
        maximumGridDifference(
            initial,
            current
        );

    if (
        maximumError !=
        0.0
    ) {
        throw std::runtime_error(
            "Zero-step CMP execution modified the grid."
        );
    }

    std::cout
        << "  [PASS] zero timesteps"
        << ", updates = 0"
        << ", barrier phases = 0\n";
}

}  // namespace


int main() {
    try {
        std::cout
            << "\n=== Heat2D CMP Integration Test ===\n\n";

        /*
         * 51 x 51 contains 49 interior rows.
         *
         * Testing 64 workers is intentional:
         * some workers receive an empty row range but must still
         * participate correctly in synchronization.
         */
        const std::vector<std::size_t> threadCounts{
            1,
            2,
            4,
            8,
            16,
            32,
            64
        };

        for (
            const std::size_t threadCount :
            threadCounts
        ) {
            testAgainstSequential(
                threadCount
            );
        }

        testZeroSteps();
        testRejectsZeroThreads();

        std::cout
            << "\nCMP integration tests: PASSED\n";

        return 0;

    } catch (
        const std::exception& error
    ) {
        std::cerr
            << "\nCMP integration tests: FAILED\n"
            << "Reason: "
            << error.what()
            << '\n';

        return 1;
    }
}
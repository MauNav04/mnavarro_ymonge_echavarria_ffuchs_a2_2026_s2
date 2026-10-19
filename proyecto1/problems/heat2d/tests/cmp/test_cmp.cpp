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
 * @brief Computes the maximum absolute cell-wise difference between two grids.
 *
 * @param first First grid to compare.
 * @param second Second grid to compare.
 * @return Maximum absolute difference across all corresponding cells.
 *
 * @throws std::invalid_argument If the grids have different dimensions.
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
 * @brief Creates the common initial state used by the integration tests.
 *
 * Each execution starts from a copy of this grid so the sequential and CMP
 * models use equivalent initial and boundary conditions.
 *
 * @param config Simulation parameters used to initialize the grid.
 * @return Initialized Heat2D grid.
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
 * @brief Validates one CMP worker configuration against the sequential reference.
 *
 * The test verifies numerical equivalence with the sequential execution and
 * checks the execution statistics reported by the CMP model, including cell
 * updates, worker count, timesteps, and synchronization phases.
 *
 * @param threadCount Number of CMP worker threads to test.
 *
 * @throws std::runtime_error If the CMP result or reported statistics violate
 *         the expected invariants.
 */
void testAgainstSequential(
    std::size_t threadCount
) {
    heat2d::SimulationConfig config;

    /*
     * Use a moderate grid that exercises row partitioning while keeping
     * the integration test inexpensive enough for frequent execution.
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
     * Compute the sequential reference from the same initial state used
     * by the CMP execution.
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
     * Both implementations evaluate the same numerical stencil from equivalent
     * states. Any difference above the tolerance indicates an unexpected
     * divergence in the CMP execution.
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
     * Each interior cell must be updated exactly once per timestep.
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

    /*
     * CMP uses two synchronization phases per timestep:
     * one after computation and one after the global state update.
     */
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
 * @brief Verifies that CMP rejects a configuration with zero workers.
 *
 * @throws std::runtime_error If the invalid configuration is accepted.
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
 * @brief Verifies the zero-timestep execution path.
 *
 * A zero-step execution must leave the grid unchanged and report no cell
 * updates or synchronization phases.
 *
 * @throws std::runtime_error If the grid or execution statistics change.
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


/**
 * @brief Runs the CMP integration test suite.
 *
 * The suite exercises several worker counts, including configurations with
 * more workers than interior rows, and validates edge cases such as zero
 * timesteps and invalid worker counts.
 *
 * @return 0 if every test passes; 1 otherwise.
 */
int main() {
    try {
        std::cout
            << "\n=== Heat2D CMP Integration Test ===\n\n";

        /*
         * A 51 x 51 grid contains 49 interior rows. Testing 64 workers is
         * intentional: some workers receive empty row ranges but must still
         * participate correctly in the synchronization protocol.
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
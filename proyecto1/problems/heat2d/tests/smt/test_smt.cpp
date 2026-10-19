#include "smt/smt.hpp"

#include "config.hpp"
#include "grid.hpp"
#include "initial_conditions.hpp"
#include "sequential.hpp"
#include "smt/cpu_topology.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <set>
#include <stdexcept>
#include <vector>

namespace {

constexpr double comparisonTolerance =
    1e-12;


/**
 * @brief Enforces a test condition.
 *
 * @param condition Condition that must evaluate to true.
 * @param message Error message reported if the condition fails.
 *
 * @throws std::runtime_error If the condition is false.
 */
void require(
    bool condition,
    const char* message
) {
    if (!condition) {
        throw std::runtime_error(
            message
        );
    }
}


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
 * Sequential and SMT executions start from independent copies of this grid,
 * ensuring equivalent initial and boundary conditions.
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
 * @brief Builds the SMT worker counts supported by the current machine.
 *
 * Standard experimental counts are included only when the process can access
 * enough logical CPUs. This keeps the integration test portable across
 * machines exposing different CPU topologies.
 *
 * @param availableLogicalCpus Number of logical CPUs visible to the process.
 * @return Supported worker counts selected for the test suite.
 */
std::vector<std::size_t> createTestThreadCounts(
    std::size_t availableLogicalCpus
) {
    const std::vector<std::size_t> candidates{
        1,
        2,
        4,
        8,
        16
    };

    std::vector<std::size_t> result;

    for (
        const std::size_t candidate :
        candidates
    ) {
        if (
            candidate <=
            availableLogicalCpus
        ) {
            result.push_back(
                candidate
            );
        }
    }

    return result;
}


/**
 * @brief Validates one SMT configuration against the sequential reference.
 *
 * The test verifies numerical equivalence and checks the main SMT execution
 * invariants: worker count, timesteps, cell updates, barrier phases, worker
 * pinning, topology metadata, and topology-aware CPU placement.
 *
 * @param threadCount Number of SMT workers to test.
 * @param topology CPU topology used to derive the expected placement.
 *
 * @throws std::runtime_error If any numerical, execution, or placement
 *         invariant is violated.
 */
void testAgainstSequential(
    std::size_t threadCount,
    const heat2d::smt::CpuTopology& topology
) {
    heat2d::SimulationConfig config;

    /*
     * Use a moderate workload that exercises parallel row partitioning and
     * synchronization while keeping the integration test inexpensive enough
     * for frequent execution.
     */
    config.rows =
        51;

    config.cols =
        51;

    config.alpha =
        0.05;

    config.dt =
        0.0001;

    config.totalTime =
        0.01;

    const std::size_t steps =
        config.steps();

    const heat2d::Grid initial =
        createInitialGrid(
            config
        );

    /*
     * Compute the sequential reference from the same initial state used by
     * the SMT execution.
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

    heat2d::Grid smtCurrent =
        initial;

    heat2d::Grid smtNext(
        config.rows,
        config.cols,
        config.boundaryTemperature
    );

    heat2d::smt::SmtConfig smtConfig;

    smtConfig.threadCount =
        threadCount;

    const heat2d::smt::SmtStats stats =
        heat2d::smt::runSmt(
            smtCurrent,
            smtNext,
            steps,
            config.alpha,
            config.dt,
            config.dx(),
            config.dy(),
            config.boundaryTemperature,
            smtConfig
        );

    /*
     * Both models evaluate the same numerical stencil from equivalent initial
     * states. Differences beyond the configured tolerance indicate an
     * unexpected divergence in the SMT execution.
     */
    const double maximumDifference =
        maximumGridDifference(
            sequentialCurrent,
            smtCurrent
        );

    require(
        maximumDifference <=
            comparisonTolerance,
        "SMT result differs from sequential reference."
    );

    /*
     * Every interior cell must be updated exactly once per timestep.
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

    require(
        stats.threadCount ==
            threadCount,
        "Incorrect SMT worker-thread count."
    );

    require(
        stats.timeSteps ==
            steps,
        "Incorrect SMT timestep count."
    );

    require(
        stats.executedCellUpdates ==
            expectedUpdates,
        "Incorrect SMT cell-update count."
    );

    /*
     * SMT uses two synchronization phases per timestep:
     * one after computation and one after the global state update.
     */
    require(
        stats.barrierPhases ==
            2ULL *
            static_cast<std::uint64_t>(
                steps
            ),
        "Incorrect SMT barrier-phase count."
    );

    /*
     * Every worker participating in a non-empty SMT execution must be pinned
     * to the logical CPU selected by the topology-aware placement policy.
     */
    require(
        stats.pinnedWorkerCount ==
            threadCount,
        "Not every SMT worker was pinned."
    );

    require(
        stats.availablePhysicalCores ==
            heat2d::smt::physicalCoreCount(
                topology
            ),
        "Incorrect physical-core metadata."
    );

    require(
        stats.availableLogicalCpus ==
            heat2d::smt::logicalCpuCount(
                topology
            ),
        "Incorrect logical-CPU metadata."
    );

    require(
        stats.logicalCpuIds.size() ==
            threadCount,
        "Incorrect number of SMT CPU assignments."
    );

    /*
     * Hardware SMT placement assigns at most one worker to each selected
     * logical CPU.
     */
    const std::set<int> assignedCpus(
        stats.logicalCpuIds.begin(),
        stats.logicalCpuIds.end()
    );

    require(
        assignedCpus.size() ==
            stats.logicalCpuIds.size(),
        "SMT assigned the same logical CPU to multiple workers."
    );

    /*
     * Worker placement must follow the topology-aware order generated by the
     * SMT subsystem.
     */
    const std::vector<int> expectedCpuOrder =
        heat2d::smt::buildSmtCpuOrder(
            topology
        );

    require(
        std::equal(
            stats.logicalCpuIds.begin(),
            stats.logicalCpuIds.end(),
            expectedCpuOrder.begin()
        ),
        "SMT worker CPU placement does not match topology order."
    );

    std::cout
        << "  [PASS] threads = "
        << threadCount
        << ", updates = "
        << stats.executedCellUpdates
        << ", barrier phases = "
        << stats.barrierPhases
        << ", max error = "
        << maximumDifference
        << ", CPUs = {";

    for (
        std::size_t index = 0;
        index < stats.logicalCpuIds.size();
        ++index
    ) {
        if (
            index != 0
        ) {
            std::cout
                << ",";
        }

        std::cout
            << stats.logicalCpuIds[
                index
            ];
    }

    std::cout
        << "}\n";
}


/**
 * @brief Verifies the zero-timestep SMT execution path.
 *
 * A zero-step execution must leave the current grid unchanged, perform no
 * cell updates or barrier phases, and avoid creating pinned worker threads.
 *
 * @throws std::runtime_error If any zero-step invariant is violated.
 */
void testZeroSteps()
{
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

    heat2d::smt::SmtConfig smtConfig;

    smtConfig.threadCount =
        1;

    const heat2d::smt::SmtStats stats =
        heat2d::smt::runSmt(
            current,
            next,
            0,
            config.alpha,
            config.dt,
            config.dx(),
            config.dy(),
            config.boundaryTemperature,
            smtConfig
        );

    require(
        stats.timeSteps == 0,
        "Zero-step SMT run reports timesteps."
    );

    require(
        stats.executedCellUpdates == 0,
        "Zero-step SMT run reports cell updates."
    );

    require(
        stats.barrierPhases == 0,
        "Zero-step SMT run reports barrier phases."
    );

    require(
        stats.pinnedWorkerCount == 0,
        "Zero-step SMT run should not create pinned workers."
    );

    require(
        maximumGridDifference(
            initial,
            current
        ) == 0.0,
        "Zero-step SMT run modified the current grid."
    );

    std::cout
        << "  [PASS] zero timesteps\n";
}


/**
 * @brief Verifies that SMT rejects a configuration with zero workers.
 *
 * @throws std::runtime_error If the invalid configuration is accepted.
 */
void testRejectsZeroThreads()
{
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

    heat2d::smt::SmtConfig smtConfig;

    smtConfig.threadCount =
        0;

    bool rejected =
        false;

    try {
        static_cast<void>(
            heat2d::smt::runSmt(
                current,
                next,
                1,
                config.alpha,
                config.dt,
                config.dx(),
                config.dy(),
                config.boundaryTemperature,
                smtConfig
            )
        );

    } catch (
        const std::invalid_argument&
    ) {
        rejected =
            true;
    }

    require(
        rejected,
        "SMT accepted zero worker threads."
    );

    std::cout
        << "  [PASS] zero worker threads rejected\n";
}


/**
 * @brief Verifies that hardware SMT rejects unsupported oversubscription.
 *
 * The requested worker count is deliberately set one above the number of
 * process-visible logical CPUs. With oversubscription disabled by the default
 * SMT configuration, the execution must reject this request.
 *
 * @param topology Detected CPU topology used to determine the invalid count.
 *
 * @throws std::runtime_error If the oversubscribed configuration is accepted.
 */
void testRejectsOversubscription(
    const heat2d::smt::CpuTopology& topology
) {
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

    heat2d::smt::SmtConfig smtConfig;

    smtConfig.threadCount =
        heat2d::smt::logicalCpuCount(
            topology
        ) +
        1;

    bool rejected =
        false;

    try {
        static_cast<void>(
            heat2d::smt::runSmt(
                current,
                next,
                1,
                config.alpha,
                config.dt,
                config.dx(),
                config.dy(),
                config.boundaryTemperature,
                smtConfig
            )
        );

    } catch (
        const std::invalid_argument&
    ) {
        rejected =
            true;
    }

    require(
        rejected,
        "SMT accepted more workers than available logical CPUs."
    );

    std::cout
        << "  [PASS] oversubscription rejected\n";
}

}  // namespace


/**
 * @brief Runs the SMT integration test suite.
 *
 * The suite adapts its worker counts to the process-visible CPU topology,
 * compares supported SMT configurations against the sequential reference,
 * and validates edge cases for zero timesteps, invalid worker counts, and
 * unsupported oversubscription.
 *
 * @return 0 if every integration test passes; 1 otherwise.
 */
int main()
{
    try {
        const heat2d::smt::CpuTopology topology =
            heat2d::smt::detectCpuTopology();

        const std::vector<std::size_t> threadCounts =
            createTestThreadCounts(
                heat2d::smt::logicalCpuCount(
                    topology
                )
            );

        std::cout
            << "\n=== Heat2D SMT Integration Test ===\n\n";

        std::cout
            << "Available physical cores: "
            << heat2d::smt::physicalCoreCount(
                topology
            )
            << '\n';

        std::cout
            << "Available logical CPUs:   "
            << heat2d::smt::logicalCpuCount(
                topology
            )
            << '\n';

        std::cout
            << "SMT available:            "
            << (
                heat2d::smt::smtAvailable(
                    topology
                )
                    ? "yes"
                    : "no"
            )
            << "\n\n";

        for (
            const std::size_t threadCount :
            threadCounts
        ) {
            testAgainstSequential(
                threadCount,
                topology
            );
        }

        testZeroSteps();

        testRejectsZeroThreads();

        testRejectsOversubscription(
            topology
        );

        std::cout
            << "\nSMT integration tests: PASSED\n";

        return 0;

    } catch (
        const std::exception& error
    ) {
        std::cerr
            << "\nSMT integration tests: FAILED\n"
            << "Reason: "
            << error.what()
            << '\n';

        return 1;
    }
}
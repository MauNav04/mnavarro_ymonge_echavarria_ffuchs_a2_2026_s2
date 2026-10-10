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
 * @brief Builds useful worker counts supported by the current machine.
 *
 * UTM may produce:
 *
 *   1, 2, 4
 *
 * while the target Ryzen should produce:
 *
 *   1, 2, 4, 8, 16
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


void testAgainstSequential(
    std::size_t threadCount,
    const heat2d::smt::CpuTopology& topology
) {
    heat2d::SimulationConfig config;

    /*
     * Moderate workload:
     *
     * 51 x 51
     * 100 timesteps
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
     * SMT execution.
     */
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

    require(
        stats.barrierPhases ==
            2ULL *
            static_cast<std::uint64_t>(
                steps
            ),
        "Incorrect SMT barrier-phase count."
    );

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

    const std::set<int> assignedCpus(
        stats.logicalCpuIds.begin(),
        stats.logicalCpuIds.end()
    );

    require(
        assignedCpus.size() ==
            stats.logicalCpuIds.size(),
        "SMT assigned the same logical CPU to multiple workers."
    );

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
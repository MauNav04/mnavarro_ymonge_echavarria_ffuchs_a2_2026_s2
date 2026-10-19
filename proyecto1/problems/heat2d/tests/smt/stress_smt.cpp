#include "smt/smt.hpp"

#include "config.hpp"
#include "grid.hpp"
#include "initial_conditions.hpp"
#include "sequential.hpp"
#include "smt/cpu_topology.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iomanip>
#include <iostream>
#include <set>
#include <stdexcept>
#include <vector>


namespace {

constexpr std::size_t repetitions =
    3;

constexpr double numericalTolerance =
    1e-12;


/**
 * @brief Stores numerical error metrics between two grid states.
 *
 * `l2Rms` represents the root-mean-square error over the complete grid,
 * while `lInfinity` stores the maximum absolute cell-wise difference.
 */
struct NumericalDifference {
    double l2Rms{0.0};

    double lInfinity{0.0};
};


/**
 * @brief Creates the common initial state used by every stress-test execution.
 *
 * Sequential and SMT runs start from independent copies of this same grid,
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
 * @brief Computes numerical differences between a reference and candidate grid.
 *
 * Calculates both the root-mean-square error and the maximum absolute
 * difference across all corresponding cells.
 *
 * @param reference Grid used as the numerical reference.
 * @param candidate Grid whose result is being validated.
 * @return Computed L2 RMS and L-infinity error metrics.
 *
 * @throws std::invalid_argument If the grids have different dimensions.
 */
NumericalDifference calculateDifference(
    const heat2d::Grid& reference,
    const heat2d::Grid& candidate
) {
    if (
        reference.rows() !=
            candidate.rows() ||
        reference.cols() !=
            candidate.cols()
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
                candidate(
                    row,
                    column
                ) -
                reference(
                    row,
                    column
                );

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

    NumericalDifference result;

    result.l2Rms =
        std::sqrt(
            squaredErrorSum /
            static_cast<double>(
                reference.size()
            )
        );

    result.lInfinity =
        maximumError;

    return result;
}


/**
 * @brief Builds the set of SMT worker counts supported by the current topology.
 *
 * Standard experimental counts are included only when the machine exposes
 * enough logical CPUs. If the maximum available logical CPU count is not one
 * of those standard values, it is added explicitly so the detected topology
 * is exercised at full capacity.
 *
 * @param availableLogicalCpus Number of logical CPUs visible to the process.
 * @return Worker counts to use during the SMT stress test.
 */
std::vector<std::size_t> createThreadCounts(
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

    /*
     * Exercise the complete visible topology when its logical CPU count
     * is not already represented by a standard experimental configuration.
     */
    if (
        !result.empty() &&
        result.back() !=
            availableLogicalCpus
    ) {
        result.push_back(
            availableLogicalCpus
        );
    }

    return result;
}


/**
 * @brief Validates execution and CPU-placement metadata reported by SMT.
 *
 * The test checks worker pinning, timestep and cell-update counts, barrier
 * phases, detected topology metadata, and the topology-aware logical CPU
 * assignment produced by the SMT model.
 *
 * @param stats Statistics returned by the SMT execution.
 * @param expectedThreads Expected number of worker threads.
 * @param expectedSteps Expected number of simulation timesteps.
 * @param expectedUpdates Expected number of interior cell updates.
 * @param topology CPU topology used to derive the expected placement.
 *
 * @throws std::runtime_error If any execution or placement invariant fails.
 */
void validateStats(
    const heat2d::smt::SmtStats& stats,
    std::size_t expectedThreads,
    std::size_t expectedSteps,
    std::uint64_t expectedUpdates,
    const heat2d::smt::CpuTopology& topology
) {
    if (
        stats.threadCount !=
        expectedThreads
    ) {
        throw std::runtime_error(
            "SMT stress test detected an incorrect worker count."
        );
    }

    if (
        stats.pinnedWorkerCount !=
        expectedThreads
    ) {
        throw std::runtime_error(
            "SMT stress test detected an unpinned worker."
        );
    }

    if (
        stats.timeSteps !=
        expectedSteps
    ) {
        throw std::runtime_error(
            "SMT stress test detected an incorrect timestep count."
        );
    }

    if (
        stats.executedCellUpdates !=
        expectedUpdates
    ) {
        throw std::runtime_error(
            "SMT stress test detected an incorrect update count."
        );
    }

    if (
        stats.barrierPhases !=
        2ULL *
        static_cast<std::uint64_t>(
            expectedSteps
        )
    ) {
        throw std::runtime_error(
            "SMT stress test detected an incorrect barrier count."
        );
    }

    if (
        stats.availablePhysicalCores !=
        heat2d::smt::physicalCoreCount(
            topology
        )
    ) {
        throw std::runtime_error(
            "SMT physical-core metadata is incorrect."
        );
    }

    if (
        stats.availableLogicalCpus !=
        heat2d::smt::logicalCpuCount(
            topology
        )
    ) {
        throw std::runtime_error(
            "SMT logical-CPU metadata is incorrect."
        );
    }

    if (
        stats.logicalCpuIds.size() !=
        expectedThreads
    ) {
        throw std::runtime_error(
            "SMT CPU-assignment count is incorrect."
        );
    }

    const std::set<int> uniqueCpuIds(
        stats.logicalCpuIds.begin(),
        stats.logicalCpuIds.end()
    );

    if (
        uniqueCpuIds.size() !=
        stats.logicalCpuIds.size()
    ) {
        throw std::runtime_error(
            "SMT assigned multiple workers to the same logical CPU."
        );
    }

    const std::vector<int> expectedCpuOrder =
        heat2d::smt::buildSmtCpuOrder(
            topology
        );

    if (
        !std::equal(
            stats.logicalCpuIds.begin(),
            stats.logicalCpuIds.end(),
            expectedCpuOrder.begin()
        )
    ) {
        throw std::runtime_error(
            "SMT CPU assignment does not match topology-aware order."
        );
    }
}


/**
 * @brief Prints the logical CPU assignment of one SMT execution.
 *
 * @param cpuIds Logical CPU IDs assigned to the worker threads.
 */
void printCpuAssignments(
    const std::vector<int>& cpuIds
) {
    std::cout
        << "{";

    for (
        std::size_t index = 0;
        index < cpuIds.size();
        ++index
    ) {
        if (
            index != 0
        ) {
            std::cout
                << ",";
        }

        std::cout
            << cpuIds[index];
    }

    std::cout
        << "}";
}

}  // namespace


/**
 * @brief Runs the SMT stress-validation suite.
 *
 * Detects the available CPU topology, evaluates every supported SMT worker
 * configuration, and compares each execution against a sequential numerical
 * reference. The suite also validates worker pinning, topology-aware CPU
 * placement, synchronization metadata, timing, throughput, and speedup.
 *
 * @return 0 if every SMT configuration passes; 1 otherwise.
 */
int main()
{
    try {
        heat2d::SimulationConfig config;

        /*
         * Use the same large workload as the CMP stress campaign so both
         * execution models are exercised under equivalent numerical work.
         */
        config.lengthX =
            1.0;

        config.lengthY =
            1.0;

        config.rows =
            801;

        config.cols =
            801;

        config.alpha =
            0.05;

        config.dt =
            0.000005;

        config.totalTime =
            0.005;

        config.maxTemperature =
            100.0;

        config.boundaryTemperature =
            0.0;

        const std::size_t steps =
            config.steps();

        if (
            steps != 1000
        ) {
            throw std::runtime_error(
                "SMT stress configuration must produce exactly 1000 timesteps."
            );
        }

        const heat2d::smt::CpuTopology topology =
            heat2d::smt::detectCpuTopology();

        const std::size_t logicalCpuCount =
            heat2d::smt::logicalCpuCount(
                topology
            );

        const std::vector<std::size_t> threadCounts =
            createThreadCounts(
                logicalCpuCount
            );

        const std::uint64_t expectedCellUpdates =
            static_cast<std::uint64_t>(
                config.rows - 2
            ) *
            static_cast<std::uint64_t>(
                config.cols - 2
            ) *
            static_cast<std::uint64_t>(
                steps
            );

        std::cout
            << "\n========================================\n"
            << " Heat2D SMT Stress Test\n"
            << "========================================\n\n";

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
            << steps
            << '\n';

        std::cout
            << "dt: "
            << config.dt
            << " s\n";

        std::cout
            << "Repetitions per configuration: "
            << repetitions
            << '\n';

        std::cout
            << "Expected cell updates: "
            << expectedCellUpdates
            << '\n';

        std::cout
            << "\n--- Detected CPU Topology ---\n";

        std::cout
            << "Linux SMT control: "
            << topology.linuxSmtControl
            << '\n';

        std::cout
            << "Physical cores: "
            << heat2d::smt::physicalCoreCount(
                topology
            )
            << '\n';

        std::cout
            << "Logical CPUs: "
            << logicalCpuCount
            << '\n';

        std::cout
            << "Maximum threads/core: "
            << heat2d::smt::maximumThreadsPerCore(
                topology
            )
            << '\n';

        std::cout
            << "SMT hardware visible: "
            << (
                heat2d::smt::smtAvailable(
                    topology
                )
                    ? "YES"
                    : "NO"
            )
            << '\n';

        /*
         * Build one common initial state so every execution starts from
         * identical numerical conditions.
         */
        const heat2d::Grid initialGrid =
            createInitialGrid(
                config
            );

        /*
         * Compute the sequential numerical and timing reference once.
         * Every SMT result is compared against this final grid.
         */
        heat2d::Grid sequentialCurrent =
            initialGrid;

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
            steps,
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
         * Exercise every worker count supported by the detected logical
         * CPU topology while preserving the same workload and initial state.
         */
        for (
            const std::size_t threadCount :
            threadCounts
        ) {
            std::cout
                << "\n--- SMT threads = "
                << threadCount
                << " ---\n";

            for (
                std::size_t repetition = 1;
                repetition <= repetitions;
                ++repetition
            ) {
                heat2d::Grid smtCurrent =
                    initialGrid;

                heat2d::Grid smtNext(
                    config.rows,
                    config.cols,
                    config.boundaryTemperature
                );

                heat2d::smt::SmtConfig smtConfig;

                smtConfig.threadCount =
                    threadCount;

                const auto start =
                    std::chrono::steady_clock::now();

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

                const auto end =
                    std::chrono::steady_clock::now();

                const double elapsedSeconds =
                    std::chrono::duration<double>(
                        end - start
                    ).count();

                validateStats(
                    stats,
                    threadCount,
                    steps,
                    expectedCellUpdates,
                    topology
                );

                const NumericalDifference difference =
                    calculateDifference(
                        sequentialCurrent,
                        smtCurrent
                    );

                if (
                    difference.lInfinity >
                    numericalTolerance
                ) {
                    throw std::runtime_error(
                        "SMT stress result differs from sequential reference."
                    );
                }

                const double speedup =
                    sequentialSeconds /
                    elapsedSeconds;

                const double throughput =
                    static_cast<double>(
                        expectedCellUpdates
                    ) /
                    elapsedSeconds;

                std::cout
                    << "Run "
                    << repetition
                    << ": time = "
                    << elapsedSeconds
                    << " s"
                    << " | speedup = "
                    << speedup
                    << "x"
                    << " | throughput = "
                    << throughput
                    << " updates/s"
                    << " | L2 = "
                    << difference.l2Rms
                    << " | Linf = "
                    << difference.lInfinity
                    << " | CPUs = ";

                printCpuAssignments(
                    stats.logicalCpuIds
                );

                std::cout
                    << '\n';
            }
        }

        std::cout
            << "\nSMT stress test: PASSED\n\n";

        return 0;

    } catch (
        const std::exception& error
    ) {
        std::cerr
            << "\nSMT stress test: FAILED\n"
            << "Reason: "
            << error.what()
            << '\n';

        return 1;
    }
}
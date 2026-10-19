#include "smt/smt.hpp"

#include "coarse_grained/reusable_barrier.hpp"
#include "heat2d.hpp"
#include "initial_conditions.hpp"
#include "smt/cpu_topology.hpp"
#include "smt/thread_affinity.hpp"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace heat2d::smt {

namespace {

/**
 * @brief Contiguous interior-row range assigned to one worker.
 *
 * The interval is:
 *
 *   [firstRow, lastRowExclusive)
 */
struct RowRange {
    std::size_t firstRow{0};
    std::size_t lastRowExclusive{0};
};

/**
 * @brief Startup gate used to prevent workers from beginning execution
 *        until every std::thread has been created successfully.
 *
 * This is intentionally separate from ReusableBarrier.
 *
 * If thread creation fails halfway through, the gate can abort the
 * already-created workers without leaving them waiting forever on a
 * barrier that expects workers that were never created.
 */
class StartGate {
public:
    /**
     * @brief Blocks until execution is released or aborted.
     *
     * @return true when workers may continue.
     * @return false when startup was aborted.
     */
    [[nodiscard]]
    bool wait()
    {
        std::unique_lock<std::mutex> lock(
            mutex_
        );

        condition_.wait(
            lock,
            [this] {
                return
                    released_ ||
                    aborted_;
            }
        );

        return !aborted_;
    }

    /**
     * @brief Releases every waiting worker.
     */
    void release()
    {
        {
            std::lock_guard<std::mutex> lock(
                mutex_
            );

            released_ =
                true;
        }

        condition_.notify_all();
    }

    /**
     * @brief Aborts startup and releases every waiting worker.
     */
    void abort()
    {
        {
            std::lock_guard<std::mutex> lock(
                mutex_
            );

            aborted_ =
                true;
        }

        condition_.notify_all();
    }

private:
    std::mutex mutex_;
    std::condition_variable condition_;
    bool released_{false};
    bool aborted_{false};
};

/**
 * @brief Safely multiplies two uint64_t values.
 * 
 * Includes explicit bounds-checking against `std::numeric_limits<std::uint64_t>::max()` 
 * to prevent silent integer overflow during execution statistics generation.
 *
 * @throws std::overflow_error If the multiplication overflows.
 */
std::uint64_t checkedMultiply64(
    std::uint64_t left,
    std::uint64_t right
) {
    if (
        left != 0 &&
        right >
            std::numeric_limits<std::uint64_t>::max() /
            left
    ) {
        throw std::overflow_error(
            "SMT execution statistics overflow."
        );
    }

    return left * right;
}

/**
 * @brief Calculates the expected number of interior cell updates safely.
 */
std::uint64_t calculateExecutedCellUpdates(
    std::size_t rows,
    std::size_t cols,
    std::size_t steps
) {
    const std::uint64_t interiorRows =
        static_cast<std::uint64_t>(
            rows - 2
        );

    const std::uint64_t interiorColumns =
        static_cast<std::uint64_t>(
            cols - 2
        );

    const std::uint64_t cellsPerStep =
        checkedMultiply64(
            interiorRows,
            interiorColumns
        );

    return checkedMultiply64(
        cellsPerStep,
        static_cast<std::uint64_t>(
            steps
        )
    );
}

/**
 * @brief Creates a balanced static partition of interior rows.
 *
 * Example:
 *
 *   10 rows
 *   3 workers
 *
 * produces workloads differing by at most one row.
 *
 * Threads may receive an empty range when there are more workers than
 * interior rows. They still participate in synchronization barriers.
 */
std::vector<RowRange> createRowRanges(
    std::size_t rows,
    std::size_t threadCount
) {
    const std::size_t interiorRows =
        rows - 2;

    const std::size_t rowsPerThread =
        interiorRows /
        threadCount;

    const std::size_t remainder =
        interiorRows %
        threadCount;

    std::vector<RowRange> ranges;

    ranges.reserve(
        threadCount
    );

    std::size_t firstRow =
        1;

    for (
        std::size_t workerId = 0;
        workerId < threadCount;
        ++workerId
    ) {
        const std::size_t assignedRows =
            rowsPerThread +
            (
                workerId < remainder
                    ? 1
                    : 0
            );

        ranges.push_back({
            firstRow,
            firstRow + assignedRows
        });

        firstRow +=
            assignedRows;
    }

    return ranges;
}

/**
 * @brief Validates SMT arguments that do not depend on CPU topology.
 *
 * Numerical stability is intentionally NOT checked here because a
 * zero-step execution must return without requiring a stable timestep,
 * matching the behavior of the sequential implementation.
 */
void validateBasicInputs(
    const Grid& current,
    const Grid& next,
    const SmtConfig& config
) {
    if (
        current.rows() != next.rows() ||
        current.cols() != next.cols()
    ) {
        throw std::invalid_argument(
            "Current and next grids must have the same dimensions."
        );
    }

    if (
        current.rows() < 3 ||
        current.cols() < 3
    ) {
        throw std::invalid_argument(
            "SMT requires a grid of at least 3x3."
        );
    }

    if (
        config.threadCount == 0
    ) {
        throw std::invalid_argument(
            "SMT requires at least one worker thread."
        );
    }
}

/**
 * @brief Joins every joinable worker.
 */
void joinWorkers(
    std::vector<std::thread>& workers
) noexcept {
    for (
        std::thread& worker :
        workers
    ) {
        if (
            worker.joinable()
        ) {
            worker.join();
        }
    }
}

}  // namespace

/**
 * @brief Main execution orchestrator for the Simultaneous Multithreading (SMT) model.
 *
 * Analyzes the CPU topology, handles thread pinning to strictly separate logical
 * processors, and executes the simulation with a dual-phase barrier.
 */
SmtStats runSmt(
    Grid& current,
    Grid& next,
    std::size_t steps,
    double alpha,
    double dt,
    double dx,
    double dy,
    double boundaryTemperature,
    const SmtConfig& config
) {
    validateBasicInputs(
        current,
        next,
        config
    );

    /*
     * Discover the topology actually available to this process.
     *
     * No assumption is made about Linux logical CPU numbering.
     */
    const CpuTopology topology =
        detectCpuTopology();

    const std::vector<int> smtCpuOrder =
        buildSmtCpuOrder(
            topology
        );

    const std::size_t physicalCores = physicalCoreCount(topology);
    const std::size_t maxSupportedWorkers = config.allowOversubscription
        ? std::max(smtCpuOrder.size(), physicalCores * 4)
        : smtCpuOrder.size();

    if (
        config.threadCount >
        maxSupportedWorkers
    ) {
        throw std::invalid_argument(
            "SMT worker count exceeds the number of logical CPUs "
            "available to the current process."
        );
    }

    SmtStats stats;

    stats.threadCount =
        config.threadCount;

    stats.timeSteps =
        steps;

    stats.availablePhysicalCores =
        physicalCoreCount(
            topology
        );

    stats.availableLogicalCpus =
        logicalCpuCount(
            topology
        );

    /*
     * Record the exact CPU placement chosen for this execution.
     * When oversubscribing, workers are mapped across the available CPUs in round-robin.
     */
    stats.logicalCpuIds.reserve(config.threadCount);
    for (std::size_t workerId = 0; workerId < config.threadCount; ++workerId) {
        stats.logicalCpuIds.push_back(
            smtCpuOrder[workerId % smtCpuOrder.size()]
        );
    }

    /*
     * A zero-step run performs no numerical computation and does not
     * create worker threads.
     */
    if (
        steps == 0
    ) {
        return stats;
    }

    /*
     * Stability validation intentionally follows the zero-step check,
     * matching the semantics of the sequential implementation.
     */
    if (
        !isStable(
            alpha,
            dt,
            dx,
            dy
        )
    ) {
        throw std::invalid_argument(
            "The selected SMT simulation parameters violate "
            "the stability condition."
        );
    }

    const DiffusionCoefficients coefficients =
        makeDiffusionCoefficients(
            alpha,
            dt,
            dx,
            dy
        );

    const std::vector<RowRange> rowRanges =
        createRowRanges(
            current.rows(),
            config.threadCount
        );

    /*
     * Synchronization objects.
     *
     * pinningBarrier:
     *   Ensures every worker has completed its affinity setup before
     *   any numerical work begins.
     *
     * timestepBarrier:
     *   Used twice per timestep:
     *
     *     phase 1 -> all workers finished writing next
     *     phase 2 -> boundary/swap completed by worker 0
     */
    ReusableBarrier pinningBarrier(
        config.threadCount
    );

    ReusableBarrier timestepBarrier(
        config.threadCount
    );

    StartGate startGate;

    std::atomic<bool> pinningFailed{
        false
    };

    std::mutex pinningErrorMutex;
    std::exception_ptr firstPinningError;

    std::vector<std::thread> workers;
    workers.reserve(
        config.threadCount
    );

    try {
        for (
            std::size_t workerId = 0;
            workerId < config.threadCount;
            ++workerId
        ) {
            workers.emplace_back(
                [
                    &,
                    workerId
                ] {
                    /*
                     * Do not begin affinity setup until every worker has
                     * been created successfully.
                     */
                    if (
                        !startGate.wait()
                    ) {
                        return;
                    }

                    try {
                        const int targetCpu =
                            stats.logicalCpuIds[
                                workerId
                            ];

                        pinCurrentThreadToCpu(
                            targetCpu
                        );

                        if (
                            !currentThreadIsPinnedToCpu(
                                targetCpu
                            )
                        ) {
                            throw std::runtime_error(
                                "SMT worker affinity verification failed."
                            );
                        }

                    } catch (...) {
                        /*
                         * Every thread must still reach pinningBarrier,
                         * otherwise the remaining workers could deadlock.
                         */
                        const bool wasAlreadyFailed =
                            pinningFailed.exchange(
                                true
                            );

                        if (
                            !wasAlreadyFailed
                        ) {
                            std::lock_guard<std::mutex> lock(
                                pinningErrorMutex
                            );

                            firstPinningError =
                                std::current_exception();
                        }
                    }

                    /*
                     * All workers complete affinity setup before any
                     * Heat2D computation begins.
                     */
                    pinningBarrier.wait();

                    if (
                        pinningFailed.load()
                    ) {
                        return;
                    }

                    const RowRange range =
                        rowRanges[
                            workerId
                        ];

                    /*
                     * Persistent worker loop.
                     *
                     * Workers are created exactly once and remain alive
                     * during every timestep.
                     */
                    for (
                        std::size_t step = 0;
                        step < steps;
                        ++step
                    ) {
                        /*
                         * Each worker writes only its own contiguous
                         * interior-row range.
                         */
                        for (
                            std::size_t row =
                                range.firstRow;
                            row <
                                range.lastRowExclusive;
                            ++row
                        ) {
                            for (
                                std::size_t column = 1;
                                column <
                                    current.cols() - 1;
                                ++column
                            ) {
                                computeCell(
                                    current,
                                    next,
                                    row,
                                    column,
                                    coefficients
                                );
                            }
                        }

                        /*
                         * Phase 1:
                         *
                         * All cells of the next state must be complete
                         * before the global timestep transition.
                         */
                        timestepBarrier.wait();

                        /*
                         * Only one worker performs global operations.
                         */
                        if (
                            workerId == 0
                        ) {
                            applyFixedBoundary(
                                next,
                                boundaryTemperature
                            );

                            std::swap(
                                current,
                                next
                            );
                        }

                        /*
                         * Phase 2:
                         *
                         * No worker may begin the next timestep before
                         * the boundary update and buffer swap complete.
                         */
                        timestepBarrier.wait();
                    }
                }
            );
        }

    } catch (...) {
        /*
         * Thread creation may fail before config.threadCount workers
         * exist. In that situation workers cannot safely wait on a
         * fixed-size barrier, so abort them while they are still
         * blocked at StartGate.
         */
        startGate.abort();

        joinWorkers(
            workers
        );

        throw;
    }

    /*
     * All workers exist. They may now proceed to CPU pinning.
     */
    startGate.release();

    joinWorkers(
        workers
    );

    /*
     * Propagate a worker pinning failure to the caller only after every
     * thread has terminated safely.
     */
    if (
        firstPinningError
    ) {
        std::rethrow_exception(
            firstPinningError
        );
    }

    stats.pinnedWorkerCount =
        config.threadCount;

    stats.executedCellUpdates =
        calculateExecutedCellUpdates(
            current.rows(),
            current.cols(),
            steps
        );

    stats.barrierPhases =
        checkedMultiply64(
            2ULL,
            static_cast<std::uint64_t>(
                steps
            )
        );

    return stats;
}

}  // namespace heat2d::smt
#include "coarse_grained/coarse_grained.hpp"

#include "coarse_grained/reusable_barrier.hpp"
#include "heat2d.hpp"
#include "initial_conditions.hpp"

#include <chrono>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace heat2d {

namespace {

/**
 * @brief Represents a half-open interval of interior rows [first, last).
 * 
 * Used for static partitioning of the spatial domain among thread workers.
 */
struct RowRange {
    std::size_t first;
    std::size_t last;
};

/**
 * @brief Multiplies two counters with strict overflow protection.
 *
 * Designed to prevent silent arithmetic wraparounds when calculating 
 * vast quantities of simulated cellular updates over thousands of timesteps.
 *
 * @throws std::overflow_error If the product exceeds uint64_t capacity.
 */
std::uint64_t checkedMultiply(
    std::uint64_t left,
    std::uint64_t right
) {
    if (
        left != 0 &&
        right >
            std::numeric_limits<std::uint64_t>::max() / left
    ) {
        throw std::overflow_error(
            "Coarse-Grained statistics overflow."
        );
    }

    return left * right;
}

/**
 * @brief Distributes the interior rows of the grid across OS threads.
 *
 * Implements a static, contiguous partitioning strategy. If the total number 
 * of interior rows does not divide evenly by the thread count, the remainder 
 * `R` rows are distributed identically (one extra row) to the first `R` workers.
 *
 * @return std::vector<RowRange> containing the exact row boundaries per worker.
 */
std::vector<RowRange> createRowRanges(
    std::size_t rows,
    std::size_t threadCount
) {
    const std::size_t interiorRows =
        rows - 2;

    const std::size_t baseRows =
        interiorRows / threadCount;

    const std::size_t remainder =
        interiorRows % threadCount;

    std::vector<RowRange> ranges;

    ranges.reserve(
        threadCount
    );

    std::size_t firstRow = 1;

    for (
        std::size_t threadId = 0;
        threadId < threadCount;
        ++threadId
    ) {
        const std::size_t assignedRows =
            baseRows +
            (
                threadId < remainder
                    ? 1
                    : 0
            );

        const std::size_t lastRow =
            firstRow + assignedRows;

        ranges.push_back({
            firstRow,
            lastRow
        });

        firstRow =
            lastRow;
    }

    return ranges;
}

/**
 * @brief Emulates a high-latency blocking event (e.g., I/O or LLC miss).
 *
 * Invokes an OS-level sleep, forcing the thread into a blocked state and 
 * yielding the CPU back to the kernel scheduler. This allows the OS to 
 * dispatch other ready threads (oversubscription latency hiding).
 * 
 * @param duration The exact wall-clock time the thread should remain suspended.
 */
void simulateCostlyBlockingEvent(
    std::chrono::nanoseconds duration
) {
    if (duration.count() <= 0) {
        return;
    }

    std::this_thread::sleep_for(
        duration
    );
}

/**
 * @brief Determines if the current row terminates a coarse processing chunk.
 *
 * @param row The absolute global index of the row being processed.
 * @param lastInteriorRow The global index of the grid's final computable row.
 * @param rowsPerBlockingEvent The frequency threshold for synthetic stalls.
 * 
 * @return True if the thread should yield the CPU after this row.
 * 
 * Note: By evaluating against the global `row` index rather than a thread-local 
 * counter, the total number of blocking events generated across the entire grid 
 * remains mathematically constant, regardless of `threadCount`.
 */
bool shouldBlockAfterRow(
    std::size_t row,
    std::size_t lastInteriorRow,
    std::size_t rowsPerBlockingEvent
) {
    const bool completedFullBlock =
        (row % rowsPerBlockingEvent) == 0;

    /*
     * The final sub-region might contain fewer rows than a full chunk,
     * but must still emit a blocking event to ensure statistical equivalence.
     */
    const bool completedFinalPartialBlock =
        row == lastInteriorRow;

    return
        completedFullBlock ||
        completedFinalPartialBlock;
}

} // namespace

/**
 * @brief Main execution orchestrator for the Coarse-Grained Multithreading (CGMT) model.
 * 
 * Spawns real OS threads (`std::thread`), chunks the grid statically, and coordinates 
 * the simulation using a multi-phase synchronization barrier. Injects periodic 
 * high-latency stalls to evaluate the OS scheduler's capability to hide latency 
 * through thread oversubscription.
 */
CoarseGrainedStats runCoarseGrained(
    Grid& current,
    Grid& next,
    std::size_t steps,
    double alpha,
    double dt,
    double dx,
    double dy,
    double boundaryTemperature,
    const CoarseGrainedConfig& coarseConfig
) {
    /*
     * Architectural assertions & Grid boundary validation.
     */
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
            "Heat2D requires a grid of at least 3 x 3."
        );
    }

    if (coarseConfig.threadCount == 0) {
        throw std::invalid_argument(
            "Coarse-Grained requires at least one worker thread."
        );
    }

    if (
        coarseConfig.blockingDuration.count() < 0
    ) {
        throw std::invalid_argument(
            "Blocking duration cannot be negative."
        );
    }

    if (
        coarseConfig.rowsPerBlockingEvent == 0
    ) {
        throw std::invalid_argument(
            "Rows per blocking event must be greater than zero."
        );
    }

    /*
     * Pre-calculate thermodynamic constants to avoid redundant float math 
     * inside the deep innermost loops.
     */
    const DiffusionCoefficients coefficients =
        makeDiffusionCoefficients(
            alpha,
            dt,
            dx,
            dy
        );

    CoarseGrainedStats stats;

    stats.threadCount =
        coarseConfig.threadCount;

    stats.timeSteps =
        steps;

    const std::uint64_t interiorRows =
        static_cast<std::uint64_t>(
            current.rows() - 2
        );

    const std::uint64_t interiorColumns =
        static_cast<std::uint64_t>(
            current.cols() - 2
        );

    /*
     * Calculate theoretical payload sizes for logging and verification.
     */
    const std::uint64_t cellsPerStep =
        checkedMultiply(
            interiorRows,
            interiorColumns
        );

    stats.executedCellUpdates =
        checkedMultiply(
            cellsPerStep,
            static_cast<std::uint64_t>(
                steps
            )
        );

    /*
     * Compute the global number of blocking events per timestep:
     * ceil(interiorRows / rowsPerBlockingEvent)
     *
     * Division + Modulo avoids potential integer overflow from the standard 
     * ceiling macro `(A + B - 1) / B`.
     */
    const std::uint64_t rowsPerEvent =
        static_cast<std::uint64_t>(
            coarseConfig.rowsPerBlockingEvent
        );

    const std::uint64_t eventsPerStep =
        (
            interiorRows /
            rowsPerEvent
        )
        +
        (
            interiorRows %
            rowsPerEvent != 0
                ? 1ULL
                : 0ULL
        );

    /*
     * If blocking duration is zero, bypass the event counter entirely.
     */
    if (
        coarseConfig.blockingDuration.count() > 0
    ) {
        stats.blockingEvents =
            checkedMultiply(
                eventsPerStep,
                static_cast<std::uint64_t>(
                    steps
                )
            );
    }

    /*
     * The simulation requires a strict two-phase synchronization barrier per timestep:
     * 1. All workers finish computing their chunks and writing to the `next` grid.
     * 2. All workers observe the pointer swap and boundary updates before proceeding.
     */
    stats.barrierPhases =
        checkedMultiply(
            2ULL,
            static_cast<std::uint64_t>(
                steps
            )
        );

    /*
     * Zero-step fast exit.
     */
    if (steps == 0) {
        return stats;
    }

    /*
     * Static and contiguous domain partitioning.
     */
    const std::vector<RowRange> ranges =
        createRowRanges(
            current.rows(),
            coarseConfig.threadCount
        );

    /*
     * All spun-up threads must participate in the dual-phase barrier, 
     * even if a thread received an empty spatial range due to over-partitioning.
     */
    ReusableBarrier barrier(
        coarseConfig.threadCount
    );

    std::vector<std::thread> workers;

    workers.reserve(
        coarseConfig.threadCount
    );

    const std::size_t lastInteriorRow =
        current.rows() - 2;

    for (
        std::size_t threadId = 0;
        threadId < coarseConfig.threadCount;
        ++threadId
    ) {
        const RowRange range =
            ranges[threadId];

        // Spawn OS-level thread capturing threadId and workload range by value.
        workers.emplace_back(
            [&, threadId, range]() {

                for (
                    std::size_t step = 0;
                    step < steps;
                    ++step
                ) {
                    /*
                     * Lock-free compute phase: Each thread reads from the shared `current` 
                     * buffer and writes to its strictly disjoint, private region in `next`.
                     */
                    for (
                        std::size_t row = range.first;
                        row < range.last;
                        ++row
                    ) {
                        for (
                            std::size_t column = 1;
                            column + 1 < current.cols();
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

                        /*
                         * Check if the thread must yield after finishing this row.
                         * The logic relies on the GLOBAL row index to ensure the total 
                         * stall volume injected into the system remains invariant.
                         */
                        if (
                            coarseConfig.blockingDuration.count() > 0 &&
                            shouldBlockAfterRow(
                                row,
                                lastInteriorRow,
                                coarseConfig.rowsPerBlockingEvent
                            )
                        ) {
                            simulateCostlyBlockingEvent(
                                coarseConfig.blockingDuration
                            );
                        }
                    }

                    /*
                     * PHASE 1: Execution Sync
                     * Ensure all threads have finished evaluating the stencil 
                     * before the main thread touches boundary conditions or swaps grids.
                     */
                    barrier.wait();

                    /*
                     * Serial execution phase: Thread 0 handles global state updates 
                     * while all other threads sleep inside the barrier.
                     */
                    if (threadId == 0) {
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
                     * PHASE 2: Swap Sync
                     * Ensure no thread begins step K+1 until Thread 0 has successfully 
                     * swapped the `current` and `next` pointers.
                     */
                    barrier.wait();
                }
            }
        );
    }

    /*
     * Join all OS threads before yielding control back to the caller.
     */
    for (
        std::thread& worker :
        workers
    ) {
        worker.join();
    }

    return stats;
}

} // namespace heat2d
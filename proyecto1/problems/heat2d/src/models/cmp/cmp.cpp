#include "cmp/cmp.hpp"

#include "coarse_grained/reusable_barrier.hpp"
#include "heat2d.hpp"
#include "initial_conditions.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace heat2d {

namespace {

/**
 * @brief Represents a half-open interval of interior rows.
 *
 * The range contains:
 *
 *   [firstRow, lastRowExclusive)
 *
 * For example:
 *
 *   firstRow = 1
 *   lastRowExclusive = 5
 *
 * means rows:
 *
 *   1, 2, 3, 4
 */
struct RowRange {
    std::size_t firstRow{0};
    std::size_t lastRowExclusive{0};
};


/**
 * @brief Builds a balanced static partition of the interior rows.
 *
 * If R interior rows are distributed across P workers:
 *
 *   q = R / P
 *   r = R % P
 *
 * The first r workers receive q + 1 rows.
 * The remaining workers receive q rows.
 *
 * Empty ranges are allowed when threadCount exceeds the number
 * of interior rows. Those workers still participate in barriers.
 */
std::vector<RowRange> createRowRanges(
    std::size_t rows,
    std::size_t threadCount
) {
    const std::size_t interiorRows =
        rows - 2;

    const std::size_t baseRows =
        interiorRows /
        threadCount;

    const std::size_t remainder =
        interiorRows %
        threadCount;

    std::vector<RowRange> ranges;

    ranges.reserve(
        threadCount
    );

    std::size_t nextFirstRow =
        1;

    for (
        std::size_t workerIndex = 0;
        workerIndex < threadCount;
        ++workerIndex
    ) {
        const std::size_t rowsForWorker =
            baseRows +
            (
                workerIndex < remainder
                    ? 1
                    : 0
            );

        const std::size_t lastRowExclusive =
            nextFirstRow +
            rowsForWorker;

        ranges.push_back({
            nextFirstRow,
            lastRowExclusive
        });

        nextFirstRow =
            lastRowExclusive;
    }

    return ranges;
}


/**
 * @brief Checks whether multiplying three size_t values would
 *        overflow uint64_t.
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

    const std::uint64_t interiorCols =
        static_cast<std::uint64_t>(
            cols - 2
        );

    const std::uint64_t timeSteps =
        static_cast<std::uint64_t>(
            steps
        );

    const std::uint64_t maximum =
        std::numeric_limits<
            std::uint64_t
        >::max();

    if (
        interiorRows != 0 &&
        interiorCols >
            maximum /
            interiorRows
    ) {
        throw std::overflow_error(
            "CMP cell-update count overflow."
        );
    }

    const std::uint64_t updatesPerStep =
        interiorRows *
        interiorCols;

    if (
        updatesPerStep != 0 &&
        timeSteps >
            maximum /
            updatesPerStep
    ) {
        throw std::overflow_error(
            "CMP cell-update count overflow."
        );
    }

    return
        updatesPerStep *
        timeSteps;
}


/**
 * @brief Validates the CMP execution parameters.
 */
void validateCmpInputs(
    const Grid& current,
    const Grid& next,
    std::size_t threadCount,
    double alpha,
    double dt,
    double dx,
    double dy
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
            "CMP requires a grid of at least 3 x 3."
        );
    }

    if (threadCount == 0) {
        throw std::invalid_argument(
            "CMP thread count must be greater than zero."
        );
    }

    if (
        !isStable(
            alpha,
            dt,
            dx,
            dy
        )
    ) {
        throw std::invalid_argument(
            "The selected simulation parameters violate "
            "the stability condition."
        );
    }
}

}  // namespace


CmpStats runCmp(
    Grid& current,
    Grid& next,
    std::size_t steps,
    double alpha,
    double dt,
    double dx,
    double dy,
    double boundaryTemperature,
    const CmpConfig& config
) {
    validateCmpInputs(
        current,
        next,
        config.threadCount,
        alpha,
        dt,
        dx,
        dy
    );

    CmpStats stats;

    stats.threadCount =
        config.threadCount;

    stats.timeSteps =
        steps;

    stats.executedCellUpdates =
        calculateExecutedCellUpdates(
            current.rows(),
            current.cols(),
            steps
        );

    stats.barrierPhases =
        static_cast<std::uint64_t>(
            steps
        ) * 2ULL;

    /*
     * No threads are needed when the simulation performs zero
     * timesteps.
     */
    if (steps == 0) {
        return stats;
    }

    /*
     * Precompute the finite-difference coefficients once.
     *
     * These coefficients are immutable during the whole simulation
     * and are therefore safely shared by all worker threads.
     */
    const DiffusionCoefficients coefficients =
        makeDiffusionCoefficients(
            alpha,
            dt,
            dx,
            dy
        );

    /*
     * Static row decomposition.
     */
    const std::vector<RowRange> rowRanges =
        createRowRanges(
            current.rows(),
            config.threadCount
        );

    /*
     * Every real worker participates in both synchronization phases.
     */
    ReusableBarrier barrier(
        config.threadCount
    );

    std::vector<std::thread> workers;

    workers.reserve(
        config.threadCount
    );

    for (
        std::size_t workerIndex = 0;
        workerIndex < config.threadCount;
        ++workerIndex
    ) {
        workers.emplace_back(
            [
                &current,
                &next,
                &barrier,
                &rowRanges,
                coefficients,
                boundaryTemperature,
                steps,
                workerIndex
            ]() {
                const RowRange range =
                    rowRanges[
                        workerIndex
                    ];

                for (
                    std::size_t step = 0;
                    step < steps;
                    ++step
                ) {
                    /*
                     * Each worker writes only to the rows assigned to
                     * it. current is shared read-only during this phase.
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
                     * wait until every worker has completed next.
                     */
                    barrier.wait();

                    /*
                     * A single worker performs the global operations
                     * required between timesteps.
                     *
                     * No worker can still be writing to next here.
                     */
                    if (workerIndex == 0) {
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
                     * prevent any worker from reading current until
                     * the boundary update and swap are complete.
                     */
                    barrier.wait();
                }
            }
        );
    }

    /*
     * Wait for every operating-system thread to finish.
     */
    for (
        std::thread& worker :
        workers
    ) {
        worker.join();
    }

    return stats;
}

}  // namespace heat2d
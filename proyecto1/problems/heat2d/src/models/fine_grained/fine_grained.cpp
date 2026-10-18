#include "fine_grained/fine_grained.hpp"

#include "fine_grained/logical_thread.hpp"
#include "fine_grained/round_robin_scheduler.hpp"
#include "heat2d.hpp"
#include "initial_conditions.hpp"

#include <stdexcept>
#include <utility>
#include <vector>

namespace heat2d::fine_grained {

namespace {

/**
 * @brief Validates the general preconditions for Fine-Grained execution.
 * 
 * Ensures the target grids have identical and valid dimensions, and that 
 * at least one logical thread context is requested. Throws `std::invalid_argument` 
 * if any configuration parameters are unacceptable before simulation starts.
 */
void validateConfiguration(
    const Grid& current,
    const Grid& next,
    std::size_t logicalThreadCount
) {
    if (
        current.rows() != next.rows() ||
        current.cols() != next.cols()
    ) {
        throw std::invalid_argument(
            "Current and next grids must have the same dimensions."
        );
    }

    if (current.rows() < 3 || current.cols() < 3) {
        throw std::invalid_argument(
            "Grid must contain at least three rows and three columns."
        );
    }

    if (logicalThreadCount == 0) {
        throw std::invalid_argument(
            "Fine-Grained execution requires at least one logical thread."
        );
    }
}

/**
 * @brief Partitions the interior grid rows across logical thread contexts.
 *
 * Divides the computational workload into contiguous blocks of rows. To handle
 * grid dimensions that do not divide evenly by the thread count, the remainder 
 * rows are distributed one-by-one to the first `N` threads.
 *
 * In edge cases where `logicalThreadCount` exceeds the total number of interior 
 * rows, the trailing logical threads will simply receive an assigned row count 
 * of zero. The logic explicitly supports these "empty" contexts without failing.
 */
std::vector<LogicalThread> createLogicalThreads(
    const Grid& grid,
    std::size_t logicalThreadCount
) {
    const std::size_t interiorRows =
        grid.rows() - 2;

    const std::size_t interiorColumns =
        grid.cols() - 2;

    const std::size_t baseRows =
        interiorRows / logicalThreadCount;

    const std::size_t remainingRows =
        interiorRows % logicalThreadCount;

    std::vector<LogicalThread> logicalThreads;

    logicalThreads.reserve(
        logicalThreadCount
    );

    std::size_t firstRow = 1;

    for (
        std::size_t id = 0;
        id < logicalThreadCount;
        ++id
    ) {
        const std::size_t assignedRows =
            baseRows + (id < remainingRows ? 1 : 0);

        const std::size_t lastRowExclusive =
            firstRow + assignedRows;

        logicalThreads.emplace_back(
            id,
            firstRow,
            lastRowExclusive,
            1,
            interiorColumns + 1
        );

        firstRow =
            lastRowExclusive;
    }

    return logicalThreads;
}

/**
 * @brief Resets all simulated thread states for the upcoming timestep.
 */
void resetLogicalThreads(
    std::vector<LogicalThread>& logicalThreads
) {
    for (LogicalThread& logicalThread : logicalThreads) {
        logicalThread.reset();
    }
}

/**
 * @brief Executes a single timestep under fine-grained software multiplexing.
 *
 * The `RoundRobinScheduler` dictates the execution order. For every scheduler 
 * dispatch, exactly ONE grid cell (quantum) is computed. This simulates an extreme 
 * scenario where context switching happens after the smallest possible unit of work,
 * exposing the software overhead associated with state tracking.
 */
std::size_t executeTimeStep(
    const Grid& current,
    Grid& next,
    const DiffusionCoefficients& coefficients,
    std::vector<LogicalThread>& logicalThreads,
    RoundRobinScheduler& scheduler
) {
    std::size_t executedQuanta = 0;

    while (true) {
        // Poll the scheduler for the next thread with pending work.
        const auto selectedThread =
            scheduler.nextThread(logicalThreads);

        if (!selectedThread.has_value()) {
            break;
        }

        LogicalThread& logicalThread =
            logicalThreads[selectedThread.value()];

        const CellPosition cell =
            logicalThread.currentCell();

        // Evaluate the discrete 5-point stencil for the given cell.
        computeCell(
            current,
            next,
            cell.row,
            cell.column,
            coefficients
        );

        // Update the logical thread's internal state machine (move to next cell).
        logicalThread.advance();

        ++executedQuanta;
    }

    return executedQuanta;
}

}  // namespace

/**
 * @brief Entry point for the Fine-Grained Multithreading (FGMT) simulation.
 * 
 * Drives the numerical resolution of the 2D Heat Equation by simulating 
 * concurrent execution through software multiplexing. It does NOT utilize 
 * real OS-level threads (`std::thread`), making it deterministic and useful 
 * for analyzing scheduling overhead without physical resource contention.
 */
FineGrainedStats runFineGrained(
    Grid& current,
    Grid& next,
    std::size_t steps,
    std::size_t logicalThreadCount,
    double alpha,
    double dt,
    double dx,
    double dy,
    double boundaryTemperature
) {
    validateConfiguration(
        current,
        next,
        logicalThreadCount
    );

    const DiffusionCoefficients coefficients =
        makeDiffusionCoefficients(
            alpha,
            dt,
            dx,
            dy
        );

    std::vector<LogicalThread> logicalThreads =
        createLogicalThreads(
            current,
            logicalThreadCount
        );

    RoundRobinScheduler scheduler(
        logicalThreadCount
    );

    FineGrainedStats stats {
        logicalThreadCount,
        steps,
        0
    };

    for (
        std::size_t step = 0;
        step < steps;
        ++step
    ) {
        stats.executedQuanta +=
            executeTimeStep(
                current,
                next,
                coefficients,
                logicalThreads,
                scheduler
            );

        applyFixedBoundary(
            next,
            boundaryTemperature
        );

        // Pointer swap to avoid massive memory allocations between steps.
        std::swap(
            current,
            next
        );

        // Re-arm state machines for the upcoming temporal iteration.
        resetLogicalThreads(
            logicalThreads
        );

        scheduler.reset();
    }

    return stats;
}

}  // namespace heat2d::fine_grained
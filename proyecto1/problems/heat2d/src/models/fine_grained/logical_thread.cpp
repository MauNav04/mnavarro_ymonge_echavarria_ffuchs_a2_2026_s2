#include "fine_grained/logical_thread.hpp"

#include <limits>
#include <stdexcept>

namespace heat2d::fine_grained {

/**
 * @brief Constructs a new simulated logical thread context.
 *
 * Establishes the exact sub-grid boundary this simulated thread is responsible for.
 * Uses half-open intervals [first, lastExclusive) for memory-safe iteration.
 *
 * @param id The unique identifier for this logical context.
 * @param firstRow The starting row index (inclusive).
 * @param lastRowExclusive The ending row index (exclusive).
 * @param firstColumn The starting column index (inclusive).
 * @param lastColumnExclusive The ending column index (exclusive).
 *
 * @throws std::invalid_argument If the provided row or column ranges are inverted.
 */
LogicalThread::LogicalThread(
    std::size_t id,
    std::size_t firstRow,
    std::size_t lastRowExclusive,
    std::size_t firstColumn,
    std::size_t lastColumnExclusive
)
    : id_(id),
      firstRow_(firstRow),
      lastRowExclusive_(lastRowExclusive),
      firstColumn_(firstColumn),
      lastColumnExclusive_(lastColumnExclusive),
      currentRow_(firstRow),
      currentColumn_(firstColumn),
      assignedCells_(0),
      completedQuanta_(0) {

    // Fast-fail on invalid topologies to prevent out-of-bounds memory accesses later.
    if (firstRow_ > lastRowExclusive_) {
        throw std::invalid_argument(
            "LogicalThread received an invalid row range."
        );
    }

    if (firstColumn_ > lastColumnExclusive_) {
        throw std::invalid_argument(
            "LogicalThread received an invalid column range."
        );
    }

    // Cache the total workload size to avoid recalculating it during execution.
    assignedCells_ = calculateAssignedCells();
}

/**
 * @brief Retrieves the logical thread identifier.
 */
std::size_t LogicalThread::id() const noexcept {
    return id_;
}

/**
 * @brief Checks if the thread's state machine has remaining cells to process.
 * 
 * A thread is considered depleted for the current timestep if its internal 
 * row pointer has reached or exceeded its exclusive boundary.
 */
bool LogicalThread::hasWork() const noexcept {
    const bool hasRows =
        currentRow_ < lastRowExclusive_;

    // Handle potential edge cases where a thread was initialized with 0 columns.
    const bool hasColumns =
        firstColumn_ < lastColumnExclusive_;

    return hasRows && hasColumns;
}

/**
 * @brief Peeks at the current spatial coordinate this thread is working on.
 *
 * @throws std::logic_error If the thread's workload is already depleted.
 */
CellPosition LogicalThread::currentCell() const {
    if (!hasWork()) {
        throw std::logic_error(
            "LogicalThread has no pending work."
        );
    }

    return {
        currentRow_,
        currentColumn_
    };
}

/**
 * @brief Advances the thread's program counter to the next cell.
 *
 * Simulates the completion of a processing quantum. Traverses the assigned
 * sub-grid in strict row-major order to maximize L1-D cache spatial locality.
 *
 * @throws std::logic_error If forced to advance beyond its assigned workload.
 */
void LogicalThread::advance() {
    if (!hasWork()) {
        throw std::logic_error(
            "LogicalThread cannot advance after completing its work."
        );
    }

    ++completedQuanta_;
    ++currentColumn_;

    // Al terminar una fila, el próximo quantum comienza
    // desde la primera columna de la siguiente fila.
    // (Row-major traversal wrap-around)
    if (currentColumn_ >= lastColumnExclusive_) {
        currentColumn_ = firstColumn_;
        ++currentRow_;
    }
}

/**
 * @brief Rewinds the thread's state machine for the next global timestep.
 * 
 * Resets the instruction pointers (row/column) back to the origin of the 
 * assigned region and zeroes out the executed quanta tracker.
 */
void LogicalThread::reset() noexcept {
    currentRow_ = firstRow_;
    currentColumn_ = firstColumn_;
    completedQuanta_ = 0;
}

/**
 * @brief Returns the total amount of cells assigned to this context.
 */
std::size_t LogicalThread::assignedCells() const noexcept {
    return assignedCells_;
}

/**
 * @brief Returns how many cells have been processed in the current timestep.
 */
std::size_t LogicalThread::completedQuanta() const noexcept {
    return completedQuanta_;
}

/**
 * @brief Computes the area of the assigned sub-grid with overflow protection.
 *
 * @throws std::overflow_error If the multiplication exceeds size_t capacity.
 */
std::size_t LogicalThread::calculateAssignedCells() const {
    const std::size_t rows =
        lastRowExclusive_ - firstRow_;

    const std::size_t columns =
        lastColumnExclusive_ - firstColumn_;

    // Prevent silent arithmetic overflow which could lead to missed updates
    // or incorrect statistical reporting.
    if (
        rows != 0 &&
        columns > std::numeric_limits<std::size_t>::max() / rows
    ) {
        throw std::overflow_error(
            "LogicalThread assigned region exceeds std::size_t capacity."
        );
    }

    return rows * columns;
}

}  // namespace heat2d::fine_grained
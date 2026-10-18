#pragma once

#include <cstddef>

namespace heat2d::fine_grained {

/**
 * @brief Identifies a cell within the Heat2D grid.
 */
struct CellPosition {
    std::size_t row;
    std::size_t column;
};

/**
 * @brief Represents the execution context of a simulated logical thread.
 *
 * LogicalThread does not represent an operating system thread.
 * It preserves the progress of a region while an external scheduler
 * decides when it may consume its next quantum.
 *
 * The class uses value semantics: copying a LogicalThread creates
 * an independent snapshot that preserves the progress reached.
 */
class LogicalThread {
public:
    /**
     * @brief Creates a logical thread associated with a rectangular region.
     *
     * Ranges use the [begin, end) convention.
     *
     * @throws std::invalid_argument if any of the ranges is inverted.
     * @throws std::overflow_error if the number of cells does not fit in std::size_t.
     */
    LogicalThread(
        std::size_t id,
        std::size_t firstRow,
        std::size_t lastRowExclusive,
        std::size_t firstColumn,
        std::size_t lastColumnExclusive
    );

    /**
     * @brief Allows copying independent snapshots of the context.
     */
    LogicalThread(const LogicalThread&) = default;

    LogicalThread& operator=(const LogicalThread&) = default;

    /**
     * @brief Allows transferring the execution context.
     */
    LogicalThread(LogicalThread&&) noexcept = default;

    LogicalThread& operator=(LogicalThread&&) noexcept = default;

    ~LogicalThread() = default;

    [[nodiscard]]
    std::size_t id() const noexcept;

    [[nodiscard]]
    bool hasWork() const noexcept;

    /**
     * @throws std::logic_error if the context has already finished.
     */
    [[nodiscard]]
    CellPosition currentCell() const;

    /**
     * @brief Consumes exactly one quantum.
     *
     * A quantum currently corresponds to one cell.
     *
     * @throws std::logic_error if there is no pending work.
     */
    void advance();

    /**
     * @brief Resets the context for a new timestep.
     */
    void reset() noexcept;

    [[nodiscard]]
    std::size_t assignedCells() const noexcept;

    [[nodiscard]]
    std::size_t completedQuanta() const noexcept;

private:
    /**
     * @brief Safely computes the size of the assigned region.
     *
     * @throws std::overflow_error if the multiplication exceeds std::size_t.
     */
    [[nodiscard]]
    std::size_t calculateAssignedCells() const;

    std::size_t id_;

    std::size_t firstRow_;
    std::size_t lastRowExclusive_;

    std::size_t firstColumn_;
    std::size_t lastColumnExclusive_;

    std::size_t currentRow_;
    std::size_t currentColumn_;

    std::size_t assignedCells_;
    std::size_t completedQuanta_;
};

}  // namespace heat2d::fine_grained
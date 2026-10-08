#include "fine_grained/logical_thread.hpp"

#include <limits>
#include <stdexcept>

namespace heat2d::fine_grained {

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

    assignedCells_ = calculateAssignedCells();
}

std::size_t LogicalThread::id() const noexcept {
    return id_;
}

bool LogicalThread::hasWork() const noexcept {
    const bool hasRows =
        currentRow_ < lastRowExclusive_;

    const bool hasColumns =
        firstColumn_ < lastColumnExclusive_;

    return hasRows && hasColumns;
}

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
    if (currentColumn_ >= lastColumnExclusive_) {
        currentColumn_ = firstColumn_;
        ++currentRow_;
    }
}

void LogicalThread::reset() noexcept {
    currentRow_ = firstRow_;
    currentColumn_ = firstColumn_;
    completedQuanta_ = 0;
}

std::size_t LogicalThread::assignedCells() const noexcept {
    return assignedCells_;
}

std::size_t LogicalThread::completedQuanta() const noexcept {
    return completedQuanta_;
}

std::size_t LogicalThread::calculateAssignedCells() const {
    const std::size_t rows =
        lastRowExclusive_ - firstRow_;

    const std::size_t columns =
        lastColumnExclusive_ - firstColumn_;

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
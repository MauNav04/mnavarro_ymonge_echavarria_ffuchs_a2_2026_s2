/**
 * @file logical_thread_test.cpp
 * @brief Unit test suite for the Fine-Grained LogicalThread context.
 *
 * This suite rigorously validates the deterministic state machine of the 
 * `LogicalThread` class. Ensuring strict correctness here is paramount, 
 * as the Round-Robin scheduler relies entirely on these contexts to traverse 
 * the computational grid safely. Any boundary violation or traversal error 
 * here would cascade into memory corruption (segfaults) or mathematically 
 * invalid stencils during the massive HPC simulation.
 */

#include "fine_grained/logical_thread.hpp"

#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <limits>
#include <type_traits>
#include <utility>

namespace {

using heat2d::fine_grained::CellPosition;
using heat2d::fine_grained::LogicalThread;

/*
 * Compile-time assertions for value semantics.
 * 
 * WHY: The Round-Robin scheduler stores `LogicalThread` instances in a 
 * `std::vector`. Reallocations of this vector (e.g., during initialization) 
 * demand robust copy and move semantics to ensure the internal state 
 * (program counters) isn't corrupted or unintentionally shared.
 */
// Corrección: Se cambia _v<Type> por <Type>::value para compatibilidad con C++11/14
static_assert(
    std::is_copy_constructible<LogicalThread>::value,
    "LogicalThread must be copy constructible."
);

static_assert(
    std::is_copy_assignable<LogicalThread>::value,
    "LogicalThread must be copy assignable."
);

static_assert(
    std::is_move_constructible<LogicalThread>::value,
    "LogicalThread must be move constructible."
);

static_assert(
    std::is_move_assignable<LogicalThread>::value,
    "LogicalThread must be move assignable."
);

static_assert(
    std::is_destructible<LogicalThread>::value,
    "LogicalThread must be destructible."
);

/**
 * @brief Fails the test gracefully if an expected condition is not met.
 *
 * WHY: We use a custom exception-throwing verifier instead of `cassert` 
 * to ensure the test suite can catch failures, report them cleanly, 
 * and exit with a standard error code without aborting abruptly.
 */
void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

/**
 * @brief Asserts that two spatial cell coordinates are perfectly equivalent.
 */
void requireSameCell(
    const CellPosition& actual,
    const CellPosition& expected
) {
    require(
        actual.row == expected.row,
        "Unexpected row in LogicalThread."
    );

    require(
        actual.column == expected.column,
        "Unexpected column in LogicalThread."
    );
}

/**
 * @brief Validates the exact path of the simulated thread's instruction pointer.
 *
 * WHY: The hardware's L1-D cache prefetcher relies heavily on contiguous memory 
 * access. This test mathematically proves that the `LogicalThread` traverses 
 * its assigned 2D bounding box in strict row-major order, maximizing spatial 
 * locality during the Fine-Grained execution.
 */
void testSequentialRegionTraversal() {
    LogicalThread logicalThread(
        0,
        1,
        3,
        1,
        4
    );

    const std::vector<CellPosition> expectedCells = {
        {1, 1},
        {1, 2},
        {1, 3},
        {2, 1},
        {2, 2},
        {2, 3}
    };

    require(
        logicalThread.id() == 0,
        "LogicalThread returned an incorrect id."
    );

    require(
        logicalThread.assignedCells() == expectedCells.size(),
        "LogicalThread calculated an incorrect number of assigned cells."
    );

    require(
        logicalThread.completedQuanta() == 0,
        "A new LogicalThread should start with zero completed quanta."
    );

    for (const CellPosition& expectedCell : expectedCells) {
        require(
            logicalThread.hasWork(),
            "LogicalThread finished before processing all assigned cells."
        );

        const CellPosition currentCell =
            logicalThread.currentCell();

        requireSameCell(
            currentCell,
            expectedCell
        );

        logicalThread.advance();
    }

    require(
        !logicalThread.hasWork(),
        "LogicalThread should have completed its assigned region."
    );

    require(
        logicalThread.completedQuanta() == expectedCells.size(),
        "LogicalThread reported an incorrect number of completed quanta."
    );
}

/**
 * @brief Ensures the thread context can be cleanly rewound.
 *
 * WHY: Heat2D requires simulating thousands of discrete timesteps. 
 * Re-instantiating `LogicalThread` objects every step would cause massive 
 * memory allocation overhead. `reset()` ensures zero-allocation recycling 
 * between iterations.
 */
void testReset() {
    LogicalThread logicalThread(
        2,
        5,
        7,
        3,
        6
    );

    logicalThread.advance();
    logicalThread.advance();

    require(
        logicalThread.completedQuanta() == 2,
        "LogicalThread should report two completed quanta before reset."
    );

    logicalThread.reset();

    require(
        logicalThread.completedQuanta() == 0,
        "LogicalThread reset did not clear completed quanta."
    );

    require(
        logicalThread.hasWork(),
        "LogicalThread should contain work after reset."
    );

    requireSameCell(
        logicalThread.currentCell(),
        {5, 3}
    );
}

/**
 * @brief Validates safe handling of threads instantiated with zero area.
 *
 * WHY: During domain decomposition, if the user requests more threads 
 * than there are interior rows, some threads will inevitably receive an 
 * empty workload. The system must gracefully park these threads rather 
 * than crashing.
 */
void testEmptyRegion() {
    LogicalThread logicalThread(
        3,
        4,
        4,
        1,
        10
    );

    require(
        !logicalThread.hasWork(),
        "An empty LogicalThread region should contain no work."
    );

    require(
        logicalThread.assignedCells() == 0,
        "An empty LogicalThread region should contain zero cells."
    );

    require(
        logicalThread.completedQuanta() == 0,
        "An empty LogicalThread should have zero completed quanta."
    );
}

/**
 * @brief Ensures the constructor rejects inverted row boundaries.
 * 
 * WHY: Fail-fast philosophy. Catching inverted boundaries at construction 
 * avoids silent infinite loops or out-of-bounds pointer math deep inside 
 * the simulation core.
 */
void testInvalidRegion() {
    bool exceptionDetected = false;

    try {
        LogicalThread(
            4,
            10,
            5,
            1,
            4
        );
    } catch (const std::invalid_argument&) {
        exceptionDetected = true;
    }

    require(
        exceptionDetected,
        "LogicalThread should reject an invalid row range."
    );
}

/**
 * @brief Asserts strict protection against accessing depleted contexts.
 *
 * WHY: If the scheduler erroneously attempts to read the position of a 
 * thread that has already finished its temporal quantum, throwing a 
 * `std::logic_error` immediately isolates the scheduler bug.
 */
void testCurrentCellAfterCompletion() {
    LogicalThread logicalThread(
        5,
        1,
        2,
        1,
        2
    );

    logicalThread.advance();

    require(
        !logicalThread.hasWork(),
        "LogicalThread should have completed its single-cell region."
    );

    bool exceptionDetected = false;

    try {
        static_cast<void>(
            logicalThread.currentCell()
        );
    } catch (const std::logic_error&) {
        exceptionDetected = true;
    }

    require(
        exceptionDetected,
        "currentCell() should fail after all work is completed."
    );
}

/**
 * @brief Asserts strict protection against advancing a depleted context.
 *
 * WHY: Similar to `testCurrentCellAfterCompletion`, this prevents the 
 * internal instruction pointers (row/column) from incrementing out of 
 * their safely allocated bounding box, preventing memory corruption.
 */
void testAdvanceAfterCompletion() {
    LogicalThread logicalThread(
        6,
        1,
        2,
        1,
        2
    );

    logicalThread.advance();

    bool exceptionDetected = false;

    try {
        logicalThread.advance();
    } catch (const std::logic_error&) {
        exceptionDetected = true;
    }

    require(
        exceptionDetected,
        "advance() should fail after all work is completed."
    );
}

/**
 * @brief Ensures the constructor rejects inverted column boundaries.
 */
void testInvalidColumnRegion() {
    bool exceptionDetected = false;

    try {
        LogicalThread(
            7,
            1,
            5,
            10,
            4
        );
    } catch (const std::invalid_argument&) {
        exceptionDetected = true;
    }

    require(
        exceptionDetected,
        "LogicalThread should reject an invalid column range."
    );
}

/**
 * @brief Validates `const` correctness of state observers.
 *
 * WHY: The framework passes configurations by `const` reference wherever 
 * possible to enforce immutability guarantees. The thread must be queryable 
 * without modifying its internal program counters.
 */
void testConstObservers() {
    const LogicalThread logicalThread(
        8,
        2,
        4,
        3,
        6
    );

    require(
        logicalThread.id() == 8,
        "Const LogicalThread returned an incorrect id."
    );

    require(
        logicalThread.hasWork(),
        "Const LogicalThread should report pending work."
    );

    require(
        logicalThread.assignedCells() == 6,
        "Const LogicalThread returned an incorrect assigned cell count."
    );

    require(
        logicalThread.completedQuanta() == 0,
        "Const LogicalThread should initially report zero completed quanta."
    );

    requireSameCell(
        logicalThread.currentCell(),
        {2, 3}
    );
}

/**
 * @brief Verifies that copying a context perfectly duplicates its state.
 *
 * WHY: When passing `LogicalThread` by value (e.g., into lambdas or 
 * different vectors), the new instance must be completely disjoint from 
 * the original to avoid shared mutable state race conditions.
 */
void testCopySemantics() {
    LogicalThread original(
        9,
        1,
        3,
        1,
        4
    );

    original.advance();
    original.advance();

    LogicalThread copy = original;

    require(
        copy.completedQuanta() == 2,
        "Copied LogicalThread did not preserve completed quanta."
    );

    requireSameCell(
        copy.currentCell(),
        original.currentCell()
    );

    copy.advance();

    require(
        copy.completedQuanta() == 3,
        "Copied LogicalThread did not advance correctly."
    );

    require(
        original.completedQuanta() == 2,
        "Advancing the copy unexpectedly modified the original."
    );
}

/**
 * @brief Verifies efficient memory transferring using r-value references.
 *
 * WHY: When vector arrays resize, `std::move` must faithfully transfer the 
 * internal instruction pointers without losing the thread's execution progress.
 */
void testMoveSemantics() {
    LogicalThread source(
        10,
        4,
        6,
        2,
        5
    );

    source.advance();

    const CellPosition expectedCell =
        source.currentCell();

    const std::size_t expectedQuanta =
        source.completedQuanta();

    LogicalThread moved =
        std::move(source);

    require(
        moved.completedQuanta() == expectedQuanta,
        "Moved LogicalThread did not preserve completed quanta."
    );

    requireSameCell(
        moved.currentCell(),
        expectedCell
    );
}

/**
 * @brief Ensures protection against integer wraparounds on massive grids.
 *
 * WHY: Modern HPC setups can simulate billions of nodes. If $Rows \times Cols$ 
 * exceeds the bit-width of `size_t`, silent integer overflow would occur, 
 * destroying execution statistics and scheduler bounds.
 */
void testAssignedCellsOverflow() {
    bool exceptionDetected = false;

    try {
        LogicalThread(
            11,
            0,
            std::numeric_limits<std::size_t>::max(),
            0,
            2
        );
    } catch (const std::overflow_error&) {
        exceptionDetected = true;
    }

    require(
        exceptionDetected,
        "LogicalThread should detect assigned cell count overflow."
    );
}

}  // namespace

int main() {
    try {
        testSequentialRegionTraversal();
        testReset();
        testEmptyRegion();

        testInvalidRegion();
        testInvalidColumnRegion();

        testConstObservers();

        testCopySemantics();
        testMoveSemantics();

        testCurrentCellAfterCompletion();
        testAdvanceAfterCompletion();

        testAssignedCellsOverflow();

        std::cout
            << "LogicalThread tests: PASSED\n";

        return 0;

    } catch (const std::exception& error) {
        std::cerr
            << "LogicalThread tests: FAILED\n"
            << "Reason: "
            << error.what()
            << '\n';

        return 1;
    }
}
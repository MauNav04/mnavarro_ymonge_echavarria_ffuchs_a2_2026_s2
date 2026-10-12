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
 * @brief Falla la prueba si una condición esperada no se cumple.
 */
void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

/**
 * @brief Comprueba que dos posiciones de celda sean iguales.
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
 * @brief Verifica el recorrido completo de una región rectangular.
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
 * @brief Comprueba que reset() restaure completamente el contexto.
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
 * @brief Comprueba que un rango vacío sea válido y no contenga trabajo.
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
 * @brief Comprueba que los rangos invertidos sean rechazados.
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
 * @brief Comprueba que no se pueda consultar una celda después de finalizar.
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
 * @brief Comprueba que advance() no pueda ejecutarse después de finalizar.
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
 * @brief Comprueba que un rango de columnas invertido sea rechazado.
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
 * @brief Comprueba que los observadores funcionen sobre objetos const.
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
 * @brief Verifica que copiar un LogicalThread preserve el progreso
 *        sin compartir estado mutable entre ambos objetos.
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
 * @brief Verifica que mover un LogicalThread conserve el estado transferido.
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
 * @brief Comprueba que una región demasiado grande no provoque
 *        overflow silencioso en el número de celdas.
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
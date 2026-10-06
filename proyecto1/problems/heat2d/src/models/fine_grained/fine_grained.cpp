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
 * @brief Valida las precondiciones generales de la ejecución Fine-Grained.
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
 * @brief Crea una partición balanceada de las filas interiores.
 *
 * Las filas se distribuyen en bloques contiguos. Si la división no
 * es exacta, los primeros hilos reciben una fila adicional.
 *
 * Los hilos sin filas son válidos y simplemente no tendrán trabajo.
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
 * @brief Reinicia todos los contextos para el siguiente timestep.
 */
void resetLogicalThreads(
    std::vector<LogicalThread>& logicalThreads
) {
    for (LogicalThread& logicalThread : logicalThreads) {
        logicalThread.reset();
    }
}

/**
 * @brief Ejecuta todos los quantums correspondientes a un timestep.
 *
 * Cada selección del scheduler permite procesar exactamente una celda.
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
        const auto selectedThread =
            scheduler.nextThread(logicalThreads);

        if (!selectedThread.has_value()) {
            break;
        }

        LogicalThread& logicalThread =
            logicalThreads[selectedThread.value()];

        const CellPosition cell =
            logicalThread.currentCell();

        computeCell(
            current,
            next,
            cell.row,
            cell.column,
            coefficients
        );

        logicalThread.advance();

        ++executedQuanta;
    }

    return executedQuanta;
}

}  // namespace

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

        std::swap(
            current,
            next
        );

        resetLogicalThreads(
            logicalThreads
        );

        scheduler.reset();
    }

    return stats;
}

}  // namespace heat2d::fine_grained
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

struct RowRange {
    std::size_t first;
    std::size_t last;
};

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
 * @brief Divide las filas interiores de forma balanceada.
 *
 * El intervalo es semiabierto: [first, last).
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
    ranges.reserve(threadCount);

    std::size_t firstRow = 1;

    for (
        std::size_t threadId = 0;
        threadId < threadCount;
        ++threadId
    ) {
        const std::size_t assignedRows =
            baseRows +
            (threadId < remainder ? 1 : 0);

        const std::size_t lastRow =
            firstRow + assignedRows;

        ranges.push_back({
            firstRow,
            lastRow
        });

        firstRow = lastRow;
    }

    return ranges;
}

/**
 * @brief Simula una operación bloqueante de alta latencia.
 *
 * sleep_for() coloca el thread en espera durante el intervalo solicitado,
 * permitiendo que el SO programe otro thread runnable.
 *
 * Esta función modela un stall de forma sintética; no representa
 * directamente la latencia física de un cache miss.
 */
void simulateCostlyBlockingEvent(
    std::chrono::nanoseconds duration
) {
    if (duration.count() <= 0) {
        return;
    }

    std::this_thread::sleep_for(duration);
}

} // namespace

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

    if (coarseConfig.blockingDuration.count() < 0) {
        throw std::invalid_argument(
            "Blocking duration cannot be negative."
        );
    }

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

    const std::uint64_t cellsPerStep =
        checkedMultiply(
            interiorRows,
            interiorColumns
        );

    stats.executedCellUpdates =
        checkedMultiply(
            cellsPerStep,
            static_cast<std::uint64_t>(steps)
        );

    if (coarseConfig.blockingDuration.count() > 0) {
        stats.blockingEvents =
            checkedMultiply(
                interiorRows,
                static_cast<std::uint64_t>(steps)
            );
    }

    /*
     * Dos fases de barrera por timestep:
     *
     * 1. Todos terminaron de calcular next.
     * 2. Todos observan el swap antes del timestep siguiente.
     */
    stats.barrierPhases =
        checkedMultiply(
            2,
            static_cast<std::uint64_t>(steps)
        );

    if (steps == 0) {
        return stats;
    }

    const std::vector<RowRange> ranges =
        createRowRanges(
            current.rows(),
            coarseConfig.threadCount
        );

    ReusableBarrier barrier(
        coarseConfig.threadCount
    );

    std::vector<std::thread> workers;
    workers.reserve(
        coarseConfig.threadCount
    );

    for (
        std::size_t threadId = 0;
        threadId < coarseConfig.threadCount;
        ++threadId
    ) {
        const RowRange range =
            ranges[threadId];

        workers.emplace_back(
            [&, threadId, range]() {

                for (
                    std::size_t step = 0;
                    step < steps;
                    ++step
                ) {
                    /*
                     * Todos los workers ejecutan concurrentemente
                     * sobre regiones distintas de next.
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
                         * Evento costoso sintético.
                         *
                         * El thread se bloquea después de completar
                         * un bloque relativamente grande: una fila.
                         */
                        simulateCostlyBlockingEvent(
                            coarseConfig.blockingDuration
                        );
                    }

                    /*
                     * Nadie puede modificar/intercambiar current y next
                     * hasta que todos hayan terminado el timestep.
                     */
                    barrier.wait();

                    /*
                     * Un único worker aplica la frontera y hace swap.
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
                     * Ningún worker comienza el timestep siguiente
                     * mientras thread 0 realiza boundary + swap.
                     */
                    barrier.wait();
                }
            }
        );
    }

    for (std::thread& worker : workers) {
        worker.join();
    }

    return stats;
}

} // namespace heat2d
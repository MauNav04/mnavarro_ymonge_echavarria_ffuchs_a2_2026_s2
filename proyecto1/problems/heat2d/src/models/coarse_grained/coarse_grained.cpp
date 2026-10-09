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
 * @brief Intervalo semiabierto de filas interiores [first, last).
 */
struct RowRange {
    std::size_t first;
    std::size_t last;
};

/**
 * @brief Multiplica dos contadores verificando overflow.
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
 * @brief Divide las filas interiores entre los workers.
 *
 * La partición es contigua y balanceada. Los primeros workers reciben
 * una fila adicional cuando la división no es exacta.
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
 * @brief Simula un evento bloqueante de alta latencia.
 *
 * sleep_for() suspende temporalmente al worker y permite que el
 * planificador del sistema operativo programe otro thread runnable.
 *
 * Esto representa un stall sintético y no debe interpretarse como
 * la latencia física exacta de un cache miss.
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
 * @brief Determina si una fila finaliza un bloque Coarse-Grained.
 *
 * Los eventos se definen utilizando el índice global de la fila,
 * independientemente del worker al que haya sido asignada.
 *
 * Esto mantiene constante el número total de eventos cuando cambia
 * threadCount.
 */
bool shouldBlockAfterRow(
    std::size_t row,
    std::size_t lastInteriorRow,
    std::size_t rowsPerBlockingEvent
) {
    const bool completedFullBlock =
        (row % rowsPerBlockingEvent) == 0;

    /*
     * La última región puede contener menos filas que el tamaño
     * completo del bloque. También debe producir un evento.
     */
    const bool completedFinalPartialBlock =
        row == lastInteriorRow;

    return
        completedFullBlock ||
        completedFinalPartialBlock;
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
    /*
     * Validación estructural.
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
     * Se calculan una única vez los coeficientes utilizados por
     * todas las actualizaciones.
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
     * Número de actualizaciones realizadas en cada timestep.
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
     * Número global de eventos bloqueantes por timestep:
     *
     * ceil(interiorRows / rowsPerBlockingEvent)
     *
     * La expresión mediante división y módulo evita overflow en
     * interiorRows + rowsPerBlockingEvent - 1.
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
     * Cuando blockingDuration es cero, no se ejecuta ningún stall.
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
     * Existen dos fases globales de barrera por timestep:
     *
     * 1. Todos terminaron de escribir next.
     * 2. Todos observan boundary + swap antes del próximo timestep.
     */
    stats.barrierPhases =
        checkedMultiply(
            2ULL,
            static_cast<std::uint64_t>(
                steps
            )
        );

    /*
     * Una simulación sin pasos no necesita crear workers.
     */
    if (steps == 0) {
        return stats;
    }

    /*
     * Partición estática y contigua del dominio.
     */
    const std::vector<RowRange> ranges =
        createRowRanges(
            current.rows(),
            coarseConfig.threadCount
        );

    /*
     * Todos los workers participan en las dos fases de barrera,
     * incluso cuando alguno recibió un rango vacío debido a
     * over-partitioning.
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

        workers.emplace_back(
            [&, threadId, range]() {

                for (
                    std::size_t step = 0;
                    step < steps;
                    ++step
                ) {
                    /*
                     * Cada worker procesa únicamente sus filas.
                     *
                     * Todos leen current y escriben regiones
                     * disjuntas de next.
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
                         * Un stall sintético ocurre únicamente después
                         * de completar un bloque grueso de filas.
                         *
                         * La condición utiliza el índice GLOBAL de la
                         * fila, por lo que el número total de eventos
                         * no depende de threadCount.
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
                     * FASE 1:
                     *
                     * Todos deben haber terminado next antes de
                     * modificar las fronteras o intercambiar mallas.
                     */
                    barrier.wait();

                    /*
                     * Un único worker realiza el trabajo serial
                     * necesario para cerrar el timestep.
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
                     * FASE 2:
                     *
                     * Ningún worker puede comenzar el siguiente
                     * timestep hasta que boundary + swap terminen.
                     */
                    barrier.wait();
                }
            }
        );
    }

    /*
     * Espera por la terminación de todos los workers.
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
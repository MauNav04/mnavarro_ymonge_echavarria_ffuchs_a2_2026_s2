#include "coarse_grained/coarse_grained.hpp"

#include "config.hpp"
#include "grid.hpp"
#include "initial_conditions.hpp"
#include "sequential.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

/**
 * @brief Calcula la diferencia absoluta máxima entre dos mallas.
 */
double maximumGridDifference(
    const heat2d::Grid& first,
    const heat2d::Grid& second
) {
    if (
        first.rows() != second.rows() ||
        first.cols() != second.cols()
    ) {
        throw std::invalid_argument(
            "Cannot compare grids with different dimensions."
        );
    }

    double maximumDifference = 0.0;

    for (
        std::size_t row = 0;
        row < first.rows();
        ++row
    ) {
        for (
            std::size_t column = 0;
            column < first.cols();
            ++column
        ) {
            maximumDifference =
                std::max(
                    maximumDifference,
                    std::abs(
                        first(row, column) -
                        second(row, column)
                    )
                );
        }
    }

    return maximumDifference;
}

/**
 * @brief Construye la condición inicial común para las pruebas.
 */
heat2d::Grid createInitialGrid(
    const heat2d::SimulationConfig& config
) {
    heat2d::Grid grid(
        config.rows,
        config.cols,
        config.boundaryTemperature
    );

    heat2d::initializeSinusoidal(
        grid,
        config.maxTemperature,
        config.lengthX,
        config.lengthY
    );

    heat2d::applyFixedBoundary(
        grid,
        config.boundaryTemperature
    );

    return grid;
}

/**
 * @brief Compara Coarse-Grained contra la referencia secuencial.
 *
 * El bloqueo sintético se desactiva para aislar la corrección
 * numérica del solver paralelo.
 */
void testAgainstSequential(
    std::size_t threadCount
) {
    heat2d::SimulationConfig config;

    config.rows = 51;
    config.cols = 51;
    config.alpha = 0.05;
    config.dt = 0.0001;
    config.totalTime = 0.01;

    const std::size_t steps =
        config.steps();

    const heat2d::Grid initial =
        createInitialGrid(config);

    /*
     * Referencia secuencial.
     */
    heat2d::Grid sequentialCurrent =
        initial;

    heat2d::Grid sequentialNext(
        config.rows,
        config.cols,
        config.boundaryTemperature
    );

    heat2d::runSequential(
        sequentialCurrent,
        sequentialNext,
        steps,
        config.alpha,
        config.dt,
        config.dx(),
        config.dy(),
        config.boundaryTemperature
    );

    /*
     * Ejecución Coarse-Grained.
     */
    heat2d::Grid coarseCurrent =
        initial;

    heat2d::Grid coarseNext(
        config.rows,
        config.cols,
        config.boundaryTemperature
    );

    heat2d::CoarseGrainedConfig coarseConfig;

    coarseConfig.threadCount =
        threadCount;

    coarseConfig.blockingDuration =
        std::chrono::nanoseconds{0};

    /*
     * Aunque el bloqueo está desactivado, usamos explícitamente
     * la política propuesta para el modelo formal.
     */
    coarseConfig.rowsPerBlockingEvent =
        8;

    const heat2d::CoarseGrainedStats stats =
        heat2d::runCoarseGrained(
            coarseCurrent,
            coarseNext,
            steps,
            config.alpha,
            config.dt,
            config.dx(),
            config.dy(),
            config.boundaryTemperature,
            coarseConfig
        );

    const double maxError =
        maximumGridDifference(
            sequentialCurrent,
            coarseCurrent
        );

    constexpr double tolerance =
        1e-12;

    if (maxError > tolerance) {
        throw std::runtime_error(
            "Coarse-Grained result differs from Sequential."
        );
    }

    const std::uint64_t expectedUpdates =
        static_cast<std::uint64_t>(
            config.rows - 2
        ) *
        static_cast<std::uint64_t>(
            config.cols - 2
        ) *
        static_cast<std::uint64_t>(
            steps
        );

    if (
        stats.executedCellUpdates !=
        expectedUpdates
    ) {
        throw std::runtime_error(
            "Unexpected number of Coarse-Grained cell updates."
        );
    }

    if (
        stats.threadCount !=
        threadCount
    ) {
        throw std::runtime_error(
            "Incorrect thread count in Coarse-Grained statistics."
        );
    }

    if (
        stats.timeSteps !=
        steps
    ) {
        throw std::runtime_error(
            "Incorrect timestep count in Coarse-Grained statistics."
        );
    }

    if (
        stats.blockingEvents != 0
    ) {
        throw std::runtime_error(
            "Blocking events were reported while blocking was disabled."
        );
    }

    const std::uint64_t expectedBarrierPhases =
        2ULL *
        static_cast<std::uint64_t>(
            steps
        );

    if (
        stats.barrierPhases !=
        expectedBarrierPhases
    ) {
        throw std::runtime_error(
            "Unexpected number of barrier phases."
        );
    }

    std::cout
        << "  [PASS] threads = "
        << threadCount
        << ", updates = "
        << stats.executedCellUpdates
        << ", max error = "
        << maxError
        << '\n';
}

/**
 * @brief Verifica que no se acepten cero workers.
 */
void testRejectsZeroThreads() {
    heat2d::SimulationConfig config;

    heat2d::Grid current =
        createInitialGrid(config);

    heat2d::Grid next(
        config.rows,
        config.cols,
        config.boundaryTemperature
    );

    heat2d::CoarseGrainedConfig coarseConfig;

    coarseConfig.threadCount = 0;

    bool rejected = false;

    try {
        const auto stats =
            heat2d::runCoarseGrained(
                current,
                next,
                config.steps(),
                config.alpha,
                config.dt,
                config.dx(),
                config.dy(),
                config.boundaryTemperature,
                coarseConfig
            );

        (void)stats;

    } catch (const std::invalid_argument&) {
        rejected = true;
    }

    if (!rejected) {
        throw std::runtime_error(
            "Coarse-Grained accepted zero threads."
        );
    }
}

/**
 * @brief Verifica que el mecanismo de bloqueo sintético realmente
 *        introduzca una espera observable.
 *
 * Este test utiliza una granularidad de una fila por evento para
 * ejercer agresivamente la ruta de bloqueo.
 */
void testBlockingEvents() {
    heat2d::SimulationConfig config;

    config.rows = 11;
    config.cols = 11;
    config.alpha = 0.05;
    config.dt = 0.0001;
    config.totalTime = 0.0005;

    const std::size_t steps =
        config.steps();

    const heat2d::Grid initial =
        createInitialGrid(config);

    /*
     * Ejecución sin bloqueo.
     */
    heat2d::Grid unblockedCurrent =
        initial;

    heat2d::Grid unblockedNext(
        config.rows,
        config.cols,
        config.boundaryTemperature
    );

    heat2d::CoarseGrainedConfig unblockedConfig;

    unblockedConfig.threadCount =
        4;

    unblockedConfig.blockingDuration =
        std::chrono::nanoseconds{0};

    /*
     * Para esta prueba específica usamos una fila por evento.
     */
    unblockedConfig.rowsPerBlockingEvent =
        1;

    const auto unblockedStart =
        std::chrono::steady_clock::now();

    const heat2d::CoarseGrainedStats unblockedStats =
        heat2d::runCoarseGrained(
            unblockedCurrent,
            unblockedNext,
            steps,
            config.alpha,
            config.dt,
            config.dx(),
            config.dy(),
            config.boundaryTemperature,
            unblockedConfig
        );

    const auto unblockedEnd =
        std::chrono::steady_clock::now();

    const double unblockedSeconds =
        std::chrono::duration<double>(
            unblockedEnd -
            unblockedStart
        ).count();

    /*
     * Ejecución con bloqueo.
     */
    heat2d::Grid blockedCurrent =
        initial;

    heat2d::Grid blockedNext(
        config.rows,
        config.cols,
        config.boundaryTemperature
    );

    heat2d::CoarseGrainedConfig blockedConfig;

    blockedConfig.threadCount =
        4;

    /*
     * 1 ms hace que el efecto sea suficientemente grande para
     * distinguirlo del overhead normal del scheduler.
     */
    blockedConfig.blockingDuration =
        std::chrono::milliseconds{1};

    blockedConfig.rowsPerBlockingEvent =
        1;

    const auto blockedStart =
        std::chrono::steady_clock::now();

    const heat2d::CoarseGrainedStats blockedStats =
        heat2d::runCoarseGrained(
            blockedCurrent,
            blockedNext,
            steps,
            config.alpha,
            config.dt,
            config.dx(),
            config.dy(),
            config.boundaryTemperature,
            blockedConfig
        );

    const auto blockedEnd =
        std::chrono::steady_clock::now();

    const double blockedSeconds =
        std::chrono::duration<double>(
            blockedEnd -
            blockedStart
        ).count();

    /*
     * El bloqueo no puede modificar el resultado numérico.
     */
    const double maxDifference =
        maximumGridDifference(
            unblockedCurrent,
            blockedCurrent
        );

    constexpr double tolerance =
        1e-12;

    if (
        maxDifference >
        tolerance
    ) {
        throw std::runtime_error(
            "Blocking events changed the numerical solution."
        );
    }

    /*
     * Malla 11x11:
     *
     * 9 filas interiores
     * 5 timesteps
     * 1 fila/evento
     *
     * 9 * 5 = 45 eventos.
     */
    const std::uint64_t expectedBlockingEvents =
        static_cast<std::uint64_t>(
            config.rows - 2
        ) *
        static_cast<std::uint64_t>(
            steps
        );

    if (
        blockedStats.blockingEvents !=
        expectedBlockingEvents
    ) {
        throw std::runtime_error(
            "Unexpected number of blocking events."
        );
    }

    if (
        unblockedStats.blockingEvents != 0
    ) {
        throw std::runtime_error(
            "Blocking events reported while blocking was disabled."
        );
    }

    /*
     * No exigimos proporcionalidad exacta debido al scheduler,
     * pero sí un incremento claramente observable.
     */
    constexpr double minimumObservableIncrease =
        0.005;

    if (
        blockedSeconds <
        unblockedSeconds +
        minimumObservableIncrease
    ) {
        throw std::runtime_error(
            "Synthetic blocking did not produce an observable delay."
        );
    }

    std::cout
        << "  [PASS] blocking simulation"
        << ", events = "
        << blockedStats.blockingEvents
        << ", no-block = "
        << unblockedSeconds
        << " s"
        << ", blocked = "
        << blockedSeconds
        << " s"
        << ", max error = "
        << maxDifference
        << '\n';
}

/**
 * @brief Verifica específicamente la nueva política de agrupación
 *        de filas usada por el modelo formal.
 *
 * Para 17 filas interiores y bloques de 8:
 *
 * ceil(17 / 8) = 3 eventos/timestep.
 *
 * Con 3 timesteps:
 *
 * 3 * 3 = 9 eventos.
 */
void testBlockingEventGrouping() {
    heat2d::SimulationConfig config;

    config.rows = 19;
    config.cols = 19;
    config.alpha = 0.05;
    config.dt = 0.0001;
    config.totalTime = 0.0003;

    const std::size_t steps =
        config.steps();

    const heat2d::Grid initial =
        createInitialGrid(config);

    /*
     * Referencia secuencial.
     */
    heat2d::Grid sequentialCurrent =
        initial;

    heat2d::Grid sequentialNext(
        config.rows,
        config.cols,
        config.boundaryTemperature
    );

    heat2d::runSequential(
        sequentialCurrent,
        sequentialNext,
        steps,
        config.alpha,
        config.dt,
        config.dx(),
        config.dy(),
        config.boundaryTemperature
    );

    /*
     * Coarse-Grained con la política formal:
     *
     * 100 us cada 8 filas.
     */
    heat2d::Grid coarseCurrent =
        initial;

    heat2d::Grid coarseNext(
        config.rows,
        config.cols,
        config.boundaryTemperature
    );

    heat2d::CoarseGrainedConfig coarseConfig;

    coarseConfig.threadCount =
        4;

    coarseConfig.blockingDuration =
        std::chrono::microseconds{100};

    coarseConfig.rowsPerBlockingEvent =
        8;

    const heat2d::CoarseGrainedStats stats =
        heat2d::runCoarseGrained(
            coarseCurrent,
            coarseNext,
            steps,
            config.alpha,
            config.dt,
            config.dx(),
            config.dy(),
            config.boundaryTemperature,
            coarseConfig
        );

    /*
     * 19 filas totales -> 17 interiores.
     *
     * ceil(17 / 8) = 3.
     */
    const std::uint64_t interiorRows =
        static_cast<std::uint64_t>(
            config.rows - 2
        );

    const std::uint64_t rowsPerEvent =
        static_cast<std::uint64_t>(
            coarseConfig.rowsPerBlockingEvent
        );

    const std::uint64_t eventsPerStep =
        interiorRows / rowsPerEvent +
        (
            interiorRows % rowsPerEvent != 0
                ? 1ULL
                : 0ULL
        );

    const std::uint64_t expectedBlockingEvents =
        eventsPerStep *
        static_cast<std::uint64_t>(
            steps
        );

    if (
        stats.blockingEvents !=
        expectedBlockingEvents
    ) {
        throw std::runtime_error(
            "Incorrect blocking-event grouping."
        );
    }

    const double maxError =
        maximumGridDifference(
            sequentialCurrent,
            coarseCurrent
        );

    constexpr double tolerance =
        1e-12;

    if (
        maxError >
        tolerance
    ) {
        throw std::runtime_error(
            "Grouped blocking changed the numerical solution."
        );
    }

    std::cout
        << "  [PASS] blocking grouping"
        << ", rows/event = "
        << coarseConfig.rowsPerBlockingEvent
        << ", events = "
        << stats.blockingEvents
        << ", max error = "
        << maxError
        << '\n';
}

/**
 * @brief Verifica que una duración bloqueante negativa sea rechazada.
 */
void testRejectsNegativeBlockingDuration() {
    heat2d::SimulationConfig config;

    heat2d::Grid current =
        createInitialGrid(config);

    heat2d::Grid next(
        config.rows,
        config.cols,
        config.boundaryTemperature
    );

    heat2d::CoarseGrainedConfig coarseConfig;

    coarseConfig.threadCount =
        4;

    coarseConfig.blockingDuration =
        std::chrono::nanoseconds{-1};

    bool rejected = false;

    try {
        const auto stats =
            heat2d::runCoarseGrained(
                current,
                next,
                config.steps(),
                config.alpha,
                config.dt,
                config.dx(),
                config.dy(),
                config.boundaryTemperature,
                coarseConfig
            );

        (void)stats;

    } catch (const std::invalid_argument&) {
        rejected = true;
    }

    if (!rejected) {
        throw std::runtime_error(
            "Coarse-Grained accepted a negative blocking duration."
        );
    }
}

/**
 * @brief Verifica que rowsPerBlockingEvent no pueda ser cero.
 */
void testRejectsZeroRowsPerBlockingEvent() {
    heat2d::SimulationConfig config;

    heat2d::Grid current =
        createInitialGrid(config);

    heat2d::Grid next(
        config.rows,
        config.cols,
        config.boundaryTemperature
    );

    heat2d::CoarseGrainedConfig coarseConfig;

    coarseConfig.threadCount =
        4;

    coarseConfig.blockingDuration =
        std::chrono::microseconds{100};

    coarseConfig.rowsPerBlockingEvent =
        0;

    bool rejected = false;

    try {
        const auto stats =
            heat2d::runCoarseGrained(
                current,
                next,
                config.steps(),
                config.alpha,
                config.dt,
                config.dx(),
                config.dy(),
                config.boundaryTemperature,
                coarseConfig
            );

        (void)stats;

    } catch (const std::invalid_argument&) {
        rejected = true;
    }

    if (!rejected) {
        throw std::runtime_error(
            "Coarse-Grained accepted zero rows per blocking event."
        );
    }
}

/**
 * @brief Verifica que una ejecución sin timesteps no modifique el estado.
 */
void testZeroSteps() {
    heat2d::SimulationConfig config;

    const heat2d::Grid initial =
        createInitialGrid(config);

    heat2d::Grid current =
        initial;

    heat2d::Grid next(
        config.rows,
        config.cols,
        config.boundaryTemperature
    );

    heat2d::CoarseGrainedConfig coarseConfig;

    coarseConfig.threadCount =
        4;

    coarseConfig.blockingDuration =
        std::chrono::microseconds{100};

    coarseConfig.rowsPerBlockingEvent =
        8;

    const heat2d::CoarseGrainedStats stats =
        heat2d::runCoarseGrained(
            current,
            next,
            0,
            config.alpha,
            config.dt,
            config.dx(),
            config.dy(),
            config.boundaryTemperature,
            coarseConfig
        );

    if (
        stats.executedCellUpdates != 0
    ) {
        throw std::runtime_error(
            "Zero-step execution reported cell updates."
        );
    }

    if (
        stats.blockingEvents != 0
    ) {
        throw std::runtime_error(
            "Zero-step execution reported blocking events."
        );
    }

    if (
        stats.barrierPhases != 0
    ) {
        throw std::runtime_error(
            "Zero-step execution reported barrier phases."
        );
    }

    const double maxError =
        maximumGridDifference(
            initial,
            current
        );

    if (
        maxError != 0.0
    ) {
        throw std::runtime_error(
            "Zero-step execution modified the grid."
        );
    }

    std::cout
        << "  [PASS] zero timesteps"
        << ", updates = 0"
        << ", blocking events = 0\n";
}

} // namespace

int main() {
    try {
        std::cout
            << "\n=== Heat2D Coarse-Grained Integration Test ===\n";

        const std::vector<std::size_t> threadCounts{
            1,
            2,
            4,
            8,
            16,
            64
        };

        for (
            const std::size_t threadCount :
            threadCounts
        ) {
            testAgainstSequential(
                threadCount
            );
        }

        testBlockingEvents();
        testBlockingEventGrouping();
        testZeroSteps();
        testRejectsZeroThreads();
        testRejectsNegativeBlockingDuration();
        testRejectsZeroRowsPerBlockingEvent();

        std::cout
            << "\nCoarse-Grained integration tests: PASSED\n";

        return 0;

    } catch (const std::exception& error) {
        std::cerr
            << "\nCoarse-Grained integration tests: FAILED\n"
            << error.what()
            << '\n';

        return 1;
    }
}
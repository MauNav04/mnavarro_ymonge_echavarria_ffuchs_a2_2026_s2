#include "coarse_grained/coarse_grained.hpp"

#include "config.hpp"
#include "grid.hpp"
#include "initial_conditions.hpp"
#include "sequential.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

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
            maximumDifference = std::max(
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

void testAgainstSequential(
    std::size_t threadCount
) {
    heat2d::SimulationConfig config;

    /*
     * Una configuración moderada mantiene el test rápido,
     * pero suficientemente grande para probar la partición.
     */
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

    /*
     * No inyectamos stalls en el test funcional.
     *
     * Primero comprobamos que el algoritmo concurrente es
     * numéricamente correcto.
     */
    coarseConfig.blockingDuration =
        std::chrono::nanoseconds{0};

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

    if (stats.blockingEvents != 0) {
        throw std::runtime_error(
            "Blocking events were reported while blocking was disabled."
        );
    }

    const std::uint64_t expectedBarrierPhases =
        static_cast<std::uint64_t>(
            2 * steps
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
     * Coarse-Grained con evento bloqueante sintético.
     */
    heat2d::Grid coarseCurrent =
        initial;

    heat2d::Grid coarseNext(
        config.rows,
        config.cols,
        config.boundaryTemperature
    );

    heat2d::CoarseGrainedConfig coarseConfig;

    coarseConfig.threadCount = 4;

    /*
     * Duración suficientemente pequeña para mantener rápido el test.
     *
     * No representa una latencia real de caché. Únicamente verifica
     * el camino de ejecución que contiene bloqueos sintéticos.
     */
    coarseConfig.blockingDuration =
        std::chrono::microseconds{100};

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

    constexpr double tolerance = 1e-12;

    if (maxError > tolerance) {
        throw std::runtime_error(
            "Blocking Coarse-Grained execution differs from Sequential."
        );
    }

    const std::uint64_t expectedBlockingEvents =
        static_cast<std::uint64_t>(
            config.rows - 2
        ) *
        static_cast<std::uint64_t>(
            steps
        );

    if (
        stats.blockingEvents !=
        expectedBlockingEvents
    ) {
        throw std::runtime_error(
            "Unexpected number of Coarse-Grained blocking events."
        );
    }

    std::cout
        << "  [PASS] blocking enabled"
        << ", events = "
        << stats.blockingEvents
        << ", max error = "
        << maxError
        << '\n';
}

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

    coarseConfig.threadCount = 4;

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

    coarseConfig.threadCount = 4;

    coarseConfig.blockingDuration =
        std::chrono::microseconds{100};

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

    if (stats.executedCellUpdates != 0) {
        throw std::runtime_error(
            "Zero-step execution reported cell updates."
        );
    }

    if (stats.blockingEvents != 0) {
        throw std::runtime_error(
            "Zero-step execution reported blocking events."
        );
    }

    if (stats.barrierPhases != 0) {
        throw std::runtime_error(
            "Zero-step execution reported barrier phases."
        );
    }

    const double maxError =
        maximumGridDifference(
            initial,
            current
        );

    if (maxError != 0.0) {
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
        testZeroSteps();
        testRejectsZeroThreads();
        testRejectsNegativeBlockingDuration();

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
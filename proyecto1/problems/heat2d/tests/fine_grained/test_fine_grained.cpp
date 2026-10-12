#include "config.hpp"
#include "fine_grained/fine_grained.hpp"
#include "grid.hpp"
#include "heat2d.hpp"
#include "initial_conditions.hpp"

#include <cmath>
#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

constexpr double comparisonTolerance = 1e-12;

/**
 * @brief Falla la prueba cuando una condición esperada no se cumple.
 */
void require(
    bool condition,
    const char* message
) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

/**
 * @brief Configuración pequeña pero suficientemente representativa
 *        para validar el modelo Fine-Grained.
 */
heat2d::SimulationConfig createTestConfiguration() {
    heat2d::SimulationConfig config;

    config.lengthX = 1.0;
    config.lengthY = 1.0;

    config.rows = 51;
    config.cols = 51;

    config.alpha = 0.05;
    config.dt = 0.0001;
    config.totalTime = 0.01;

    config.maxTemperature = 100.0;
    config.boundaryTemperature = 0.0;

    return config;
}

/**
 * @brief Inicializa una malla usando exactamente las mismas condiciones
 *        empleadas por el modelo base.
 */
void initializeGrid(
    heat2d::Grid& grid,
    const heat2d::SimulationConfig& config
) {
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
}

/**
 * @brief Ejecuta la implementación secuencial usada como referencia.
 */
void runSequentialReference(
    heat2d::Grid& current,
    heat2d::Grid& next,
    const heat2d::SimulationConfig& config
) {
    for (
        std::size_t step = 0;
        step < config.steps();
        ++step
    ) {
        heat2d::diffusionStep(
            current,
            next,
            config.alpha,
            config.dt,
            config.dx(),
            config.dy(),
            config.boundaryTemperature
        );

        std::swap(
            current,
            next
        );
    }
}

/**
 * @brief Calcula la máxima diferencia absoluta entre dos mallas.
 */
double calculateMaximumDifference(
    const heat2d::Grid& first,
    const heat2d::Grid& second
) {
    require(
        first.rows() == second.rows() &&
        first.cols() == second.cols(),
        "Cannot compare grids with different dimensions."
    );

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
            const double difference =
                std::abs(
                    first(row, column) -
                    second(row, column)
                );

            if (difference > maximumDifference) {
                maximumDifference = difference;
            }
        }
    }

    return maximumDifference;
}

/**
 * @brief Verifica que todos los bordes mantengan la temperatura fija.
 */
void validateBoundaries(
    const heat2d::Grid& grid,
    double boundaryTemperature
) {
    for (
        std::size_t column = 0;
        column < grid.cols();
        ++column
    ) {
        require(
            std::abs(
                grid(0, column) -
                boundaryTemperature
            ) <= comparisonTolerance,
            "Top boundary was modified."
        );

        require(
            std::abs(
                grid(grid.rows() - 1, column) -
                boundaryTemperature
            ) <= comparisonTolerance,
            "Bottom boundary was modified."
        );
    }

    for (
        std::size_t row = 0;
        row < grid.rows();
        ++row
    ) {
        require(
            std::abs(
                grid(row, 0) -
                boundaryTemperature
            ) <= comparisonTolerance,
            "Left boundary was modified."
        );

        require(
            std::abs(
                grid(row, grid.cols() - 1) -
                boundaryTemperature
            ) <= comparisonTolerance,
            "Right boundary was modified."
        );
    }
}

/**
 * @brief Ejecuta Fine-Grained con una cantidad determinada
 *        de hilos lógicos y lo compara con la referencia secuencial.
 */
void testFineGrainedAgainstSequential(
    const heat2d::SimulationConfig& config,
    const heat2d::Grid& sequentialResult,
    std::size_t logicalThreadCount
) {
    heat2d::Grid current(
        config.rows,
        config.cols,
        config.boundaryTemperature
    );

    heat2d::Grid next(
        config.rows,
        config.cols,
        config.boundaryTemperature
    );

    initializeGrid(
        current,
        config
    );

    const heat2d::fine_grained::FineGrainedStats stats =
        heat2d::fine_grained::runFineGrained(
            current,
            next,
            config.steps(),
            logicalThreadCount,
            config.alpha,
            config.dt,
            config.dx(),
            config.dy(),
            config.boundaryTemperature
        );

    const std::size_t interiorCells =
        (config.rows - 2) *
        (config.cols - 2);

    const std::size_t expectedQuanta =
        interiorCells *
        config.steps();

    require(
        stats.logicalThreadCount == logicalThreadCount,
        "Fine-Grained reported an incorrect logical thread count."
    );

    require(
        stats.timeSteps == config.steps(),
        "Fine-Grained reported an incorrect timestep count."
    );

    require(
        stats.executedQuanta == expectedQuanta,
        "Fine-Grained executed an incorrect number of quanta."
    );

    validateBoundaries(
        current,
        config.boundaryTemperature
    );

    const double maximumDifference =
        calculateMaximumDifference(
            sequentialResult,
            current
        );

    require(
        maximumDifference <= comparisonTolerance,
        "Fine-Grained result differs from sequential reference."
    );

    std::cout
        << "  [PASS] logical threads = "
        << logicalThreadCount
        << ", quanta = "
        << stats.executedQuanta
        << ", max error = "
        << maximumDifference
        << '\n';
}

/**
 * @brief Comprueba configuraciones con diferentes cantidades
 *        de hilos lógicos.
 */
void testMultipleLogicalThreadCounts() {
    const heat2d::SimulationConfig config =
        createTestConfiguration();

    require(
        heat2d::isStable(
            config.alpha,
            config.dt,
            config.dx(),
            config.dy()
        ),
        "Fine-Grained test configuration must be stable."
    );

    heat2d::Grid sequentialCurrent(
        config.rows,
        config.cols,
        config.boundaryTemperature
    );

    heat2d::Grid sequentialNext(
        config.rows,
        config.cols,
        config.boundaryTemperature
    );

    initializeGrid(
        sequentialCurrent,
        config
    );

    runSequentialReference(
        sequentialCurrent,
        sequentialNext,
        config
    );

    const std::vector<std::size_t> logicalThreadCounts = {
        1,
        2,
        4,
        8,
        64
    };

    for (
        const std::size_t logicalThreadCount :
        logicalThreadCounts
    ) {
        testFineGrainedAgainstSequential(
            config,
            sequentialCurrent,
            logicalThreadCount
        );
    }
}

/**
 * @brief Verifica que cero hilos lógicos sea rechazado.
 */
void testZeroLogicalThreadsAreRejected() {
    const heat2d::SimulationConfig config =
        createTestConfiguration();

    heat2d::Grid current(
        config.rows,
        config.cols,
        config.boundaryTemperature
    );

    heat2d::Grid next(
        config.rows,
        config.cols,
        config.boundaryTemperature
    );

    initializeGrid(
        current,
        config
    );

    bool exceptionDetected = false;

    try {
        static_cast<void>(
            heat2d::fine_grained::runFineGrained(
                current,
                next,
                config.steps(),
                0,
                config.alpha,
                config.dt,
                config.dx(),
                config.dy(),
                config.boundaryTemperature
            )
        );
    } catch (const std::invalid_argument&) {
        exceptionDetected = true;
    }

    require(
        exceptionDetected,
        "Fine-Grained should reject zero logical threads."
    );
}

/**
 * @brief Verifica una ejecución válida sin timesteps.
 */
void testZeroTimeSteps() {
    const heat2d::SimulationConfig config =
        createTestConfiguration();

    heat2d::Grid initial(
        config.rows,
        config.cols,
        config.boundaryTemperature
    );

    heat2d::Grid current(
        config.rows,
        config.cols,
        config.boundaryTemperature
    );

    heat2d::Grid next(
        config.rows,
        config.cols,
        config.boundaryTemperature
    );

    initializeGrid(
        initial,
        config
    );

    initializeGrid(
        current,
        config
    );

    const auto stats =
        heat2d::fine_grained::runFineGrained(
            current,
            next,
            0,
            4,
            config.alpha,
            config.dt,
            config.dx(),
            config.dy(),
            config.boundaryTemperature
        );

    require(
        stats.timeSteps == 0,
        "Zero-step simulation reported incorrect timestep count."
    );

    require(
        stats.executedQuanta == 0,
        "Zero-step simulation should execute zero quanta."
    );

    const double maximumDifference =
        calculateMaximumDifference(
            initial,
            current
        );

    require(
        maximumDifference <= comparisonTolerance,
        "Zero-step simulation modified the initial grid."
    );
}
/**
 * @brief Verifica una sobrepartición extrema del dominio.
 *
 * La malla posee únicamente cinco filas interiores, pero se crean
 * 5000 contextos lógicos. Solo los primeros cinco reciben filas;
 * los demás deben permanecer inactivos sin afectar el resultado.
 */
void testMoreLogicalThreadsThanInteriorRows() {
    heat2d::SimulationConfig config;

    config.lengthX = 1.0;
    config.lengthY = 1.0;

    config.rows = 7;
    config.cols = 7;

    config.alpha = 0.05;
    config.dt = 0.0001;
    config.totalTime = 0.001;

    config.maxTemperature = 100.0;
    config.boundaryTemperature = 0.0;

    require(
        heat2d::isStable(
            config.alpha,
            config.dt,
            config.dx(),
            config.dy()
        ),
        "Overpartition test configuration must be stable."
    );

    heat2d::Grid sequentialCurrent(
        config.rows,
        config.cols,
        config.boundaryTemperature
    );

    heat2d::Grid sequentialNext(
        config.rows,
        config.cols,
        config.boundaryTemperature
    );

    initializeGrid(
        sequentialCurrent,
        config
    );

    runSequentialReference(
        sequentialCurrent,
        sequentialNext,
        config
    );

    constexpr std::size_t logicalThreadCount = 5000;

    testFineGrainedAgainstSequential(
        config,
        sequentialCurrent,
        logicalThreadCount
    );
}
/**
 * @brief Verifica la malla mínima válida del modelo Heat2D.
 *
 * Una malla 3x3 contiene exactamente una celda interior.
 * El test comprueba que esa única celda pueda distribuirse
 * correctamente incluso cuando existen múltiples hilos lógicos.
 */
void testMinimumGrid() {
    heat2d::SimulationConfig config;

    config.lengthX = 1.0;
    config.lengthY = 1.0;

    config.rows = 3;
    config.cols = 3;

    config.alpha = 0.05;
    config.dt = 0.0001;
    config.totalTime = 0.001;

    config.maxTemperature = 100.0;
    config.boundaryTemperature = 0.0;

    require(
        heat2d::isStable(
            config.alpha,
            config.dt,
            config.dx(),
            config.dy()
        ),
        "Minimum-grid test configuration must be stable."
    );

    heat2d::Grid sequentialCurrent(
        config.rows,
        config.cols,
        config.boundaryTemperature
    );

    heat2d::Grid sequentialNext(
        config.rows,
        config.cols,
        config.boundaryTemperature
    );

    initializeGrid(
        sequentialCurrent,
        config
    );

    runSequentialReference(
        sequentialCurrent,
        sequentialNext,
        config
    );

    const std::vector<std::size_t> logicalThreadCounts = {
        1,
        2,
        8,
        64
    };

    for (
        const std::size_t logicalThreadCount :
        logicalThreadCounts
    ) {
        testFineGrainedAgainstSequential(
            config,
            sequentialCurrent,
            logicalThreadCount
        );
    }
}
}  // namespace

int main() {
    try {
        std::cout
            << "\n=== Heat2D Fine-Grained Integration Test ===\n";

        testMultipleLogicalThreadCounts();

        testMoreLogicalThreadsThanInteriorRows();
        testMinimumGrid();

        testZeroLogicalThreadsAreRejected();
        testZeroTimeSteps();

        std::cout
            << "\nFine-Grained integration tests: PASSED\n";

        return 0;

    } catch (const std::exception& error) {
        std::cerr
            << "\nFine-Grained integration tests: FAILED\n"
            << "Reason: "
            << error.what()
            << '\n';

        return 1;
    }
}
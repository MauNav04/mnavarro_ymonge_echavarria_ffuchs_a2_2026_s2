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

/**
 * @brief Allowable floating-point drift for numerical validation.
 * 
 * Used when asserting that the Fine-Grained execution matches the strictly 
 * sequential baseline. 1e-12 accommodates IEEE-754 summation variations 
 * caused by out-of-order execution, while catching structural logic bugs.
 */
constexpr double comparisonTolerance = 1e-12;

/**
 * @brief Asserts an expected condition, halting execution on failure.
 *
 * Provides a localized testing mechanism that throws a standard exception 
 * instead of invoking `std::abort` like the `assert` macro, allowing the 
 * test suite to exit gracefully.
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
 * @brief Generates a stable, computationally light 2D test scenario.
 *
 * 51x51 resolution guarantees sufficient interior cells to validate 
 * thread partitioning and Round-Robin distribution without causing 
 * excessive integration test execution time.
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
 * @brief Consistently initializes a test grid.
 *
 * Applies the sinusoidal thermal wave and fixed Dirichlet boundary conditions 
 * required for deterministic evaluation.
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
 * @brief Generates the single-threaded baseline "ground truth" grid.
 *
 * The Sequential model relies purely on a nested loop structure without 
 * synchronization overhead, making it the mathematical reference standard 
 * for validating the concurrency models.
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
 * @brief Computes the maximum absolute delta between two grids.
 * 
 * Evaluates every corresponding cell across both matrices. Used to detect 
 * non-deterministic execution flaws or boundary-condition overwrites.
 *
 * @throws std::runtime_error If the two grids have different dimensions.
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
 * @brief Asserts that Dirichlet boundary conditions were not violated.
 *
 * Ensures the concurrent models didn't incorrectly process perimeter 
 * cells (rows 0/N, columns 0/N) during their stencil execution phases.
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
 * @brief Validates a single Fine-Grained run against the sequential ground truth.
 *
 * Executes the Fine-Grained model, compares its output grid against the provided 
 * sequential reference, and asserts the generated metadata (executed quanta, 
 * logical thread count).
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

    // Verify mathematical correctness of the Round-Robin dispatcher.
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

    // Verify boundary constraints remain unbroken.
    validateBoundaries(
        current,
        config.boundaryTemperature
    );

    // Assert computational parity with the Serial model.
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
 * @brief Executes parity checks across exponential thread configurations.
 * 
 * Verifies that the internal grid-partitioning and task-scheduling logic 
 * scales correctly from 1 to 64 contexts without dropping cell updates.
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
 * @brief Asserts that the API structurally rejects zero-thread configurations.
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
                0, // Illegal logical thread count
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
 * @brief Asserts stability when requested to execute zero timesteps.
 * 
 * Verifies that the model cleanly exits without initiating execution loops 
 * or modifying the source grid.
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
            0, // Zero time steps
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
 * @brief Validates the spatial partitioner against extreme oversubscription.
 *
 * Configures 5000 logical contexts for a domain containing only 5 interior rows.
 * Asserts that the Round-Robin dispatcher correctly handles the massive volume 
 * of empty (depleted) threads without crashing or miscalculating results.
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
 * @brief Validates execution behavior on the smallest permissible grid size.
 *
 * A 3x3 grid contains exactly one interior computational cell.
 * Tests if the scheduler properly executes the simulation when N threads 
 * compete for exactly 1 unit of work.
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

/**
 * @brief Central execution suite for the Fine-Grained model integration tests.
 * 
 * Verifies parity against the ground truth sequential solver across varying 
 * topological extremes (thread scaling, minimum grids, massive oversubscription).
 */
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
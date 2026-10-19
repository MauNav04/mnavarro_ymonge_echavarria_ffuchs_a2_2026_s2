#include "config.hpp"
#include "fine_grained/fine_grained.hpp"
#include "grid.hpp"
#include "heat2d.hpp"
#include "initial_conditions.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

constexpr double pi =
    3.14159265358979323846;

constexpr double boundaryTolerance =
    1e-12;

constexpr double maximumL2RmsError =
    5e-4;

constexpr double maximumLInfinityError =
    1e-3;

/**
 * The Fine-Grained model is deterministic because logical contexts
 * are executed cooperatively in an explicit Round-Robin order.
 */
constexpr double determinismTolerance =
    0.0;

constexpr std::size_t defaultGridSize =
    801;

constexpr std::size_t defaultSteps =
    1000;

constexpr std::size_t defaultRepetitions =
    3;


struct AnalyticalError {
    double l2Rms;
    double lInfinity;
};


struct StressRunResult {
    heat2d::Grid grid;
    heat2d::fine_grained::FineGrainedStats stats;

    double elapsedSeconds;
    double l2Rms;
    double lInfinity;
};


/**
 * @brief Parses a positive integer from the command line.
 */
std::size_t parsePositiveInteger(
    const char* argument,
    const char* name
) {
    const std::string value(argument);

    std::size_t processedCharacters = 0;

    const unsigned long long parsed =
        std::stoull(
            value,
            &processedCharacters
        );

    if (
        processedCharacters != value.size() ||
        parsed == 0
    ) {
        throw std::invalid_argument(
            std::string(name) +
            " must be a positive integer."
        );
    }

    if (
        parsed >
        static_cast<unsigned long long>(
            std::numeric_limits<std::size_t>::max()
        )
    ) {
        throw std::overflow_error(
            std::string(name) +
            " exceeds std::size_t capacity."
        );
    }

    return static_cast<std::size_t>(
        parsed
    );
}


/**
 * @brief Performs a checked multiplication for workload counters.
 */
std::size_t checkedMultiply(
    std::size_t first,
    std::size_t second,
    const char* message
) {
    if (
        first != 0 &&
        second >
            std::numeric_limits<std::size_t>::max() /
            first
    ) {
        throw std::overflow_error(message);
    }

    return first * second;
}


/**
 * @brief Calculates a stable time step with a 20 percent safety margin.
 */
double calculateStableDt(
    const heat2d::SimulationConfig& config
) {
    const double inverseDxSquared =
        1.0 /
        (
            config.dx() *
            config.dx()
        );

    const double inverseDySquared =
        1.0 /
        (
            config.dy() *
            config.dy()
        );

    const double maximumDt =
        0.5 /
        (
            config.alpha *
            (
                inverseDxSquared +
                inverseDySquared
            )
        );

    return 0.8 * maximumDt;
}


/**
 * @brief Calculates the expected number of Fine-Grained quanta.
 *
 * One quantum corresponds to exactly one interior-cell update.
 */
std::size_t calculateExpectedQuanta(
    const heat2d::SimulationConfig& config,
    std::size_t steps
) {
    const std::size_t interiorRows =
        config.rows - 2;

    const std::size_t interiorColumns =
        config.cols - 2;

    const std::size_t interiorCells =
        checkedMultiply(
            interiorRows,
            interiorColumns,
            "Interior-cell count exceeds std::size_t capacity."
        );

    return checkedMultiply(
        interiorCells,
        steps,
        "Expected quantum count exceeds std::size_t capacity."
    );
}


/**
 * @brief Ensures every temperature produced by the simulation is finite.
 */
void validateFiniteTemperatures(
    const heat2d::Grid& grid
) {
    for (
        std::size_t row = 0;
        row < grid.rows();
        ++row
    ) {
        for (
            std::size_t column = 0;
            column < grid.cols();
            ++column
        ) {
            if (
                !std::isfinite(
                    grid(row, column)
                )
            ) {
                throw std::runtime_error(
                    "Stress test found a non-finite temperature."
                );
            }
        }
    }
}


/**
 * @brief Validates the fixed Dirichlet boundaries.
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
        const double topError =
            std::abs(
                grid(0, column) -
                boundaryTemperature
            );

        const double bottomError =
            std::abs(
                grid(
                    grid.rows() - 1,
                    column
                ) -
                boundaryTemperature
            );

        if (
            topError > boundaryTolerance ||
            bottomError > boundaryTolerance
        ) {
            throw std::runtime_error(
                "Stress test detected an invalid horizontal boundary."
            );
        }
    }

    for (
        std::size_t row = 0;
        row < grid.rows();
        ++row
    ) {
        const double leftError =
            std::abs(
                grid(row, 0) -
                boundaryTemperature
            );

        const double rightError =
            std::abs(
                grid(
                    row,
                    grid.cols() - 1
                ) -
                boundaryTemperature
            );

        if (
            leftError > boundaryTolerance ||
            rightError > boundaryTolerance
        ) {
            throw std::runtime_error(
                "Stress test detected an invalid vertical boundary."
            );
        }
    }
}


/**
 * @brief Evaluates the analytical solution of the sinusoidal mode.
 */
double analyticalTemperature(
    double x,
    double y,
    double time,
    const heat2d::SimulationConfig& config
) {
    const double spatialX =
        std::sin(
            pi *
            x /
            config.lengthX
        );

    const double spatialY =
        std::sin(
            pi *
            y /
            config.lengthY
        );

    const double inverseLengthXSquared =
        1.0 /
        (
            config.lengthX *
            config.lengthX
        );

    const double inverseLengthYSquared =
        1.0 /
        (
            config.lengthY *
            config.lengthY
        );

    const double decay =
        std::exp(
            -config.alpha *
            pi *
            pi *
            (
                inverseLengthXSquared +
                inverseLengthYSquared
            ) *
            time
        );

    return
        config.maxTemperature *
        spatialX *
        spatialY *
        decay;
}


/**
 * @brief Calculates RMS L2 and L-infinity errors against
 *        the analytical solution.
 */
AnalyticalError calculateAnalyticalError(
    const heat2d::Grid& grid,
    const heat2d::SimulationConfig& config,
    double simulatedTime
) {
    double squaredErrorSum = 0.0;
    double maximumError = 0.0;

    for (
        std::size_t row = 0;
        row < grid.rows();
        ++row
    ) {
        const double y =
            static_cast<double>(row) *
            config.dy();

        for (
            std::size_t column = 0;
            column < grid.cols();
            ++column
        ) {
            const double x =
                static_cast<double>(column) *
                config.dx();

            const double exact =
                analyticalTemperature(
                    x,
                    y,
                    simulatedTime,
                    config
                );

            const double error =
                std::abs(
                    grid(row, column) -
                    exact
                );

            squaredErrorSum +=
                error * error;

            maximumError =
                std::max(
                    maximumError,
                    error
                );
        }
    }

    const double l2Rms =
        std::sqrt(
            squaredErrorSum /
            static_cast<double>(
                grid.size()
            )
        );

    return {
        l2Rms,
        maximumError
    };
}


/**
 * @brief Fails the stress test when the numerical solution exceeds
 *        the accepted analytical-error bounds.
 */
void validateAnalyticalError(
    double l2Rms,
    double lInfinity
) {
    if (!std::isfinite(l2Rms)) {
        throw std::runtime_error(
            "L2 RMS analytical error is not finite."
        );
    }

    if (!std::isfinite(lInfinity)) {
        throw std::runtime_error(
            "L-infinity analytical error is not finite."
        );
    }

    if (
        l2Rms >
        maximumL2RmsError
    ) {
        throw std::runtime_error(
            "L2 RMS analytical error exceeded the allowed tolerance."
        );
    }

    if (
        lInfinity >
        maximumLInfinityError
    ) {
        throw std::runtime_error(
            "L-infinity analytical error exceeded the allowed tolerance."
        );
    }
}


/**
 * @brief Calculates the maximum absolute difference between two grids.
 */
double calculateMaximumDifference(
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
            const double difference =
                std::abs(
                    first(row, column) -
                    second(row, column)
                );

            maximumDifference =
                std::max(
                    maximumDifference,
                    difference
                );
        }
    }

    return maximumDifference;
}


/**
 * @brief Executes and validates one complete Fine-Grained simulation.
 */
StressRunResult executeStressRun(
    const heat2d::SimulationConfig& config,
    std::size_t steps,
    std::size_t logicalThreadCount,
    std::size_t expectedQuanta
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

    heat2d::initializeSinusoidal(
        current,
        config.maxTemperature,
        config.lengthX,
        config.lengthY
    );

    heat2d::applyFixedBoundary(
        current,
        config.boundaryTemperature
    );

    const auto start =
        std::chrono::steady_clock::now();

    const heat2d::fine_grained::FineGrainedStats stats =
        heat2d::fine_grained::runFineGrained(
            current,
            next,
            steps,
            logicalThreadCount,
            config.alpha,
            config.dt,
            config.dx(),
            config.dy(),
            config.boundaryTemperature
        );

    const auto end =
        std::chrono::steady_clock::now();

    const double elapsedSeconds =
        std::chrono::duration<double>(
            end - start
        ).count();

    if (
        stats.executedQuanta !=
        expectedQuanta
    ) {
        throw std::runtime_error(
            "Stress test detected an incorrect quantum count."
        );
    }

    if (
        stats.timeSteps !=
        steps
    ) {
        throw std::runtime_error(
            "Stress test detected an incorrect timestep count."
        );
    }

    if (
        stats.logicalThreadCount !=
        logicalThreadCount
    ) {
        throw std::runtime_error(
            "Stress test detected an incorrect logical-thread count."
        );
    }

    validateFiniteTemperatures(
        current
    );

    validateBoundaries(
        current,
        config.boundaryTemperature
    );

    const double simulatedTime =
        static_cast<double>(steps) *
        config.dt;

    const AnalyticalError analyticalError =
        calculateAnalyticalError(
            current,
            config,
            simulatedTime
        );

    validateAnalyticalError(
        analyticalError.l2Rms,
        analyticalError.lInfinity
    );

    return {
        std::move(current),
        stats,
        elapsedSeconds,
        analyticalError.l2Rms,
        analyticalError.lInfinity
    };
}


/**
 * @brief Prints the configuration used by the stress test.
 */
void printConfiguration(
    const heat2d::SimulationConfig& config,
    std::size_t logicalThreadCount,
    std::size_t steps,
    std::size_t repetitions,
    std::size_t expectedQuanta
) {
    std::cout
        << "\n=== Heat2D Fine-Grained Stress Test ===\n";

    std::cout
        << std::fixed
        << std::setprecision(9);

    std::cout
        << "Grid: "
        << config.rows
        << " x "
        << config.cols
        << '\n';

    std::cout
        << "Logical threads: "
        << logicalThreadCount
        << '\n';

    std::cout
        << "Steps: "
        << steps
        << '\n';

    std::cout
        << "Repetitions: "
        << repetitions
        << '\n';

    std::cout
        << "dt: "
        << config.dt
        << " s\n";

    std::cout
        << "Expected quanta per repetition: "
        << expectedQuanta
        << '\n';

    std::cout
        << "Maximum L2 RMS error: "
        << maximumL2RmsError
        << " C\n";

    std::cout
        << "Maximum L-infinity error: "
        << maximumLInfinityError
        << " C\n";
}

}  // namespace


int main(
    int argc,
    char* argv[]
) {
    try {
        if (
            argc < 2 ||
            argc > 5
        ) {
            throw std::invalid_argument(
                "Usage: ./heat2d_fine_grained_stress "
                "<logical_threads> "
                "[grid_size] "
                "[steps] "
                "[repetitions]"
            );
        }

        const std::size_t logicalThreadCount =
            parsePositiveInteger(
                argv[1],
                "Logical thread count"
            );

        const std::size_t gridSize =
            argc >= 3
                ? parsePositiveInteger(
                    argv[2],
                    "Grid size"
                )
                : defaultGridSize;

        const std::size_t steps =
            argc >= 4
                ? parsePositiveInteger(
                    argv[3],
                    "Steps"
                )
                : defaultSteps;

        const std::size_t repetitions =
            argc >= 5
                ? parsePositiveInteger(
                    argv[4],
                    "Repetitions"
                )
                : defaultRepetitions;

        if (gridSize < 3) {
            throw std::invalid_argument(
                "Grid size must be at least 3."
            );
        }

        heat2d::SimulationConfig config;

        config.lengthX = 1.0;
        config.lengthY = 1.0;

        config.rows = gridSize;
        config.cols = gridSize;

        config.alpha = 0.05;

        config.maxTemperature = 100.0;
        config.boundaryTemperature = 0.0;

        config.dt =
            calculateStableDt(
                config
            );

        config.totalTime =
            static_cast<double>(steps) *
            config.dt;

        if (
            !heat2d::isStable(
                config.alpha,
                config.dt,
                config.dx(),
                config.dy()
            )
        ) {
            throw std::runtime_error(
                "Generated stress configuration is unstable."
            );
        }

        const std::size_t expectedQuanta =
            calculateExpectedQuanta(
                config,
                steps
            );

        printConfiguration(
            config,
            logicalThreadCount,
            steps,
            repetitions,
            expectedQuanta
        );

        std::cout
            << "\n--- Reference execution ---\n";

        StressRunResult referenceResult =
            executeStressRun(
                config,
                steps,
                logicalThreadCount,
                expectedQuanta
            );

        const double referenceQuantaPerSecond =
            static_cast<double>(
                referenceResult.stats.executedQuanta
            ) /
            referenceResult.elapsedSeconds;

        std::cout
            << "Executed quanta: "
            << referenceResult.stats.executedQuanta
            << '\n';

        std::cout
            << "Execution time: "
            << referenceResult.elapsedSeconds
            << " s\n";

        std::cout
            << "Quanta per second: "
            << referenceQuantaPerSecond
            << '\n';

        std::cout
            << "L2 RMS error: "
            << referenceResult.l2Rms
            << " C\n";

        std::cout
            << "L-infinity error: "
            << referenceResult.lInfinity
            << " C\n";

        std::cout
            << "Analytical validation: PASS\n";

        double totalExecutionTime =
            referenceResult.elapsedSeconds;

        std::cout
            << "\n--- Determinism validation ---\n";

        if (repetitions == 1) {
            std::cout
                << "Only one repetition requested; "
                << "determinism comparison skipped.\n";
        }

        for (
            std::size_t repetition = 1;
            repetition < repetitions;
            ++repetition
        ) {
            StressRunResult currentResult =
                executeStressRun(
                    config,
                    steps,
                    logicalThreadCount,
                    expectedQuanta
                );

            totalExecutionTime +=
                currentResult.elapsedSeconds;

            const double maximumDifference =
                calculateMaximumDifference(
                    referenceResult.grid,
                    currentResult.grid
                );

            if (
                maximumDifference >
                determinismTolerance
            ) {
                throw std::runtime_error(
                    "Repeated executions produced different numerical results."
                );
            }

            if (
                currentResult.stats.executedQuanta !=
                referenceResult.stats.executedQuanta
            ) {
                throw std::runtime_error(
                    "Repeated executions produced different quantum counts."
                );
            }

            if (
                currentResult.stats.timeSteps !=
                referenceResult.stats.timeSteps
            ) {
                throw std::runtime_error(
                    "Repeated executions produced different timestep counts."
                );
            }

            std::cout
                << "  [PASS] Repetition "
                << repetition + 1
                << " | time = "
                << currentResult.elapsedSeconds
                << " s"
                << " | max difference = "
                << maximumDifference
                << '\n';
        }

        const double averageExecutionTime =
            totalExecutionTime /
            static_cast<double>(
                repetitions
            );

        const double averageQuantaPerSecond =
            static_cast<double>(
                expectedQuanta
            ) /
            averageExecutionTime;

        std::cout
            << "\n--- Summary ---\n";

        std::cout
            << "Repetitions completed: "
            << repetitions
            << '\n';

        std::cout
            << "Average execution time: "
            << averageExecutionTime
            << " s\n";

        std::cout
            << "Average quanta per second: "
            << averageQuantaPerSecond
            << '\n';

        std::cout
            << "Determinism tolerance: "
            << determinismTolerance
            << '\n';

        std::cout
            << "Analytical error validation: PASS\n";

        if (repetitions > 1) {
            std::cout
                << "Determinism validation: PASS\n";
        }

        std::cout
            << "\nFine-Grained stress test: PASSED\n";

        return 0;

    } catch (const std::exception& error) {
        std::cerr
            << "\nFine-Grained stress test: FAILED\n"
            << "Reason: "
            << error.what()
            << '\n';

        return 1;
    }
}
#include "coarse_grained/coarse_grained.hpp"

#include "config.hpp"
#include "grid.hpp"
#include "heat2d.hpp"
#include "initial_conditions.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

/**
 * @brief High-precision constant for the mathematical evaluation of the thermal wave.
 */
constexpr double kPi =
    3.141592653589793238462643383279502884;

/**
 * @brief Maximum allowable Root-Mean-Square (RMS) error against the analytical truth.
 * 
 * Provides an aggregated metric of how much the simulated grid has drifted from 
 * the exact mathematical formula over the course of the simulation.
 */
constexpr double kL2Tolerance = 5e-4;

/**
 * @brief Maximum allowable absolute peak error against the analytical truth.
 *
 * Ensures no single cell in the domain has accumulated a severe localized error 
 * due to floating-point imprecision or threading race conditions.
 */
constexpr double kLInfTolerance = 1e-3;

/**
 * @brief Struct encapsulating the aggregated drift metrics.
 */
struct ErrorMetrics {
    double l2{0.0};
    double lInf{0.0};
};

/**
 * @brief CLI parser for extracting dimensions and thread configurations safely.
 * 
 * Defends against buffer overflows and negative values prior to array allocations.
 *
 * @throws std::invalid_argument If unparseable or zero.
 * @throws std::out_of_range If the parameter exceeds size_t boundaries.
 */
std::size_t parsePositiveSize(
    const char* text,
    const char* argumentName
) {
    const std::string value(text);

    std::size_t consumed = 0;

    const unsigned long long parsed =
        std::stoull(
            value,
            &consumed
        );

    if (
        consumed != value.size() ||
        parsed == 0
    ) {
        throw std::invalid_argument(
            std::string(argumentName) +
            " must be a positive integer."
        );
    }

    if (
        parsed >
        static_cast<unsigned long long>(
            std::numeric_limits<std::size_t>::max()
        )
    ) {
        throw std::out_of_range(
            std::string(argumentName) +
            " is too large."
        );
    }

    return static_cast<std::size_t>(
        parsed
    );
}

/**
 * @brief Computes the absolute maximum temporal step (dt) permissible.
 *
 * Based on the Courant-Friedrichs-Lewy (CFL) constraint for the 2D Heat Equation:
 * alpha * dt * (1/dx^2 + 1/dy^2) <= 1/2
 * 
 * If the user's configuration exceeds this exact value, the Forward-Euler 
 * numerical method will mathematically blow up (infinite divergence).
 */
double maximumStableDt(
    double alpha,
    double dx,
    double dy
) {
    if (
        alpha <= 0.0 ||
        dx <= 0.0 ||
        dy <= 0.0
    ) {
        throw std::invalid_argument(
            "alpha, dx and dy must be positive."
        );
    }

    const double inverseDxSquared =
        1.0 / (dx * dx);

    const double inverseDySquared =
        1.0 / (dy * dy);

    return 0.5 /
        (
            alpha *
            (
                inverseDxSquared +
                inverseDySquared
            )
        );
}

/**
 * @brief Exact mathematical resolution of the sinusoidal initial condition.
 *
 * T(x,y,t) = Tmax * sin(pi*x/Lx) * sin(pi*y/Ly) * exp[-alpha * pi^2 * (1/Lx^2 + 1/Ly^2) * t]
 * 
 * This pure mathematical solution serves as the unwavering baseline against which 
 * the numeric approximation of the Coarse-Grained model is stress-tested.
 */
double analyticalTemperature(
    double x,
    double y,
    double time,
    const heat2d::SimulationConfig& config
) {
    const double spatialX =
        std::sin(
            kPi * x / config.lengthX
        );

    const double spatialY =
        std::sin(
            kPi * y / config.lengthY
        );

    const double decayRate =
        config.alpha *
        kPi *
        kPi *
        (
            1.0 /
                (
                    config.lengthX *
                    config.lengthX
                )
            +
            1.0 /
                (
                    config.lengthY *
                    config.lengthY
                )
        );

    const double temporal =
        std::exp(
            -decayRate * time
        );

    return
        config.maxTemperature *
        spatialX *
        spatialY *
        temporal;
}

/**
 * @brief Aggregates the overall numerical drift accumulated over the simulation.
 *
 * Evaluates the absolute discrepancy between the parallel numerical simulation 
 * and the exact analytical solution for every single interior grid cell.
 * 
 * @return ErrorMetrics Containing L2 RMS and L-infinity statistics.
 * @throws std::runtime_error If the grid lacks interior cells to sample.
 */
ErrorMetrics calculateAnalyticalError(
    const heat2d::Grid& numerical,
    const heat2d::SimulationConfig& config,
    double simulatedTime
) {
    long double squaredErrorSum = 0.0L;

    double maximumError = 0.0;

    std::uint64_t sampleCount = 0;

    for (
        std::size_t row = 1;
        row + 1 < numerical.rows();
        ++row
    ) {
        const double y =
            static_cast<double>(row) *
            config.dy();

        for (
            std::size_t column = 1;
            column + 1 < numerical.cols();
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

            const double difference =
                std::abs(
                    numerical(row, column) -
                    exact
                );

            squaredErrorSum +=
                static_cast<long double>(
                    difference
                ) *
                static_cast<long double>(
                    difference
                );

            maximumError =
                std::max(
                    maximumError,
                    difference
                );

            ++sampleCount;
        }
    }

    if (sampleCount == 0) {
        throw std::runtime_error(
            "No interior cells available for error analysis."
        );
    }

    ErrorMetrics metrics;

    metrics.l2 =
        std::sqrt(
            static_cast<double>(
                squaredErrorSum /
                static_cast<long double>(
                    sampleCount
                )
            )
        );

    metrics.lInf =
        maximumError;

    return metrics;
}

/**
 * @brief Fast check to identify if the numerical method has mathematically blown up.
 * 
 * Race conditions or CFL condition violations frequently result in NaN 
 * (Not a Number) or Infinity cascades. This immediately halts testing if detected.
 */
void validateFiniteGrid(
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
                    "Grid contains a non-finite temperature."
                );
            }
        }
    }
}

/**
 * @brief Validates the structural integrity of the Dirichlet Boundary Conditions.
 *
 * Ensures the concurrent workers haven't accidentally written outside their 
 * assigned interior bounds and corrupted the constant-temperature perimeter.
 */
double maximumBoundaryError(
    const heat2d::Grid& grid,
    double expectedTemperature
) {
    double maximumError = 0.0;

    for (
        std::size_t column = 0;
        column < grid.cols();
        ++column
    ) {
        maximumError = std::max(
            maximumError,
            std::abs(
                grid(0, column) -
                expectedTemperature
            )
        );

        maximumError = std::max(
            maximumError,
            std::abs(
                grid(
                    grid.rows() - 1,
                    column
                ) -
                expectedTemperature
            )
        );
    }

    for (
        std::size_t row = 0;
        row < grid.rows();
        ++row
    ) {
        maximumError = std::max(
            maximumError,
            std::abs(
                grid(row, 0) -
                expectedTemperature
            )
        );

        maximumError = std::max(
            maximumError,
            std::abs(
                grid(
                    row,
                    grid.cols() - 1
                ) -
                expectedTemperature
            )
        );
    }

    return maximumError;
}

/**
 * @brief Evaluates the maximum scalar delta between two resulting grids.
 * 
 * Used during determinism checks to guarantee that successive executions 
 * of the exact same parameters yield mathematically identical outputs, proving 
 * the absence of thread synchronization flaws.
 * 
 * @throws std::invalid_argument If grid topologies differ.
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
 * @brief Centralizes the instantiation of the sinusoidal test domain.
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
 * @brief Command Line Interface guide.
 */
void printUsage(
    const char* programName
) {
    std::cerr
        << "Usage:\n"
        << "  "
        << programName
        << " <threads> [grid_size] [steps] [repetitions]\n\n"
        << "Example:\n"
        << "  "
        << programName
        << " 4 801 1000 3\n";
}

} // namespace

/**
 * @brief Stress test suite for the Coarse-Grained model.
 * 
 * Focuses strictly on numerical correctness, boundary stability, and execution 
 * determinism. Runs identical topologies multiple times and mathematically 
 * validates the final distributions against an exact, closed-form analytical solution.
 */
int main(
    int argc,
    char* argv[]
) {
    try {
        if (
            argc < 2 ||
            argc > 5
        ) {
            printUsage(
                argv[0]
            );

            return 1;
        }

        const std::size_t threadCount =
            parsePositiveSize(
                argv[1],
                "Thread count"
            );

        std::size_t gridSize = 801;
        std::size_t steps = 1000;
        std::size_t repetitions = 3;

        if (argc >= 3) {
            gridSize =
                parsePositiveSize(
                    argv[2],
                    "Grid size"
                );
        }

        if (argc >= 4) {
            steps =
                parsePositiveSize(
                    argv[3],
                    "Steps"
                );
        }

        if (argc >= 5) {
            repetitions =
                parsePositiveSize(
                    argv[4],
                    "Repetitions"
                );
        }

        if (gridSize < 3) {
            throw std::invalid_argument(
                "Grid size must be at least 3."
            );
        }

        heat2d::SimulationConfig config;

        config.rows = gridSize;
        config.cols = gridSize;

        /*
         * The exact sinusoidal analytical solution requires the borders 
         * to remain strictly at 0.0 to act as infinite heat sinks.
         */
        config.boundaryTemperature = 0.0;

        /*
         * Calculate a resilient temporal delta.
         * By targeting 80% of the maximum theoretical limit, we ensure 
         * the discretization remains stable while avoiding the severe inaccuracy 
         * that occurs exactly at the CFL edge.
         */
        const double dtMaximum =
            maximumStableDt(
                config.alpha,
                config.dx(),
                config.dy()
            );

        config.dt =
            0.8 * dtMaximum;

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
                "Stress configuration is numerically unstable."
            );
        }

        // Rigorous calculation of expected payloads to assert against the output stats.
        const std::uint64_t interiorRows =
            static_cast<std::uint64_t>(
                gridSize - 2
            );

        const std::uint64_t interiorColumns =
            static_cast<std::uint64_t>(
                gridSize - 2
            );

        if (
            interiorRows != 0 &&
            interiorColumns >
                std::numeric_limits<std::uint64_t>::max() /
                interiorRows
        ) {
            throw std::overflow_error(
                "Cell-update count overflow."
            );
        }

        const std::uint64_t cellsPerStep =
            interiorRows *
            interiorColumns;

        if (
            cellsPerStep != 0 &&
            static_cast<std::uint64_t>(steps) >
                std::numeric_limits<std::uint64_t>::max() /
                cellsPerStep
        ) {
            throw std::overflow_error(
                "Cell-update count overflow."
            );
        }

        const std::uint64_t expectedUpdates =
            cellsPerStep *
            static_cast<std::uint64_t>(
                steps
            );

        const double simulatedTime =
            static_cast<double>(steps) *
            config.dt;

        heat2d::CoarseGrainedConfig coarseConfig;

        coarseConfig.threadCount =
            threadCount;

        /*
         * Since this is a structural stress-test, we disable artificial latency.
         * We want to test pure numerical and synchronization correctness without 
         * introducing unpredictable thread sleep delays from the OS.
         */
        coarseConfig.blockingDuration =
            std::chrono::nanoseconds{0};

        std::cout
            << "\n=== Heat2D Coarse-Grained Stress Test ===\n";

        std::cout
            << std::fixed
            << std::setprecision(9);

        std::cout
            << "Grid: "
            << gridSize
            << " x "
            << gridSize
            << '\n';

        std::cout
            << "Threads: "
            << threadCount
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
            << "dx = "
            << config.dx()
            << '\n';

        std::cout
            << "dy = "
            << config.dy()
            << '\n';

        std::cout
            << "dt_max = "
            << dtMaximum
            << '\n';

        std::cout
            << "dt = "
            << config.dt
            << " (80% stability limit)\n";

        std::cout
            << "Simulated time = "
            << simulatedTime
            << " s\n";

        std::cout
            << "Expected cell updates = "
            << expectedUpdates
            << '\n';

        std::cout
            << "Blocking duration = 0 ns\n\n";

        std::vector<double> elapsedTimes;
        elapsedTimes.reserve(
            repetitions
        );

        std::unique_ptr<heat2d::Grid>
            deterministicReference;

        bool deterministic = true;

        double referenceL2 = 0.0;
        double referenceLInf = 0.0;

        for (
            std::size_t repetition = 0;
            repetition < repetitions;
            ++repetition
        ) {
            heat2d::Grid current =
                createInitialGrid(
                    config
                );

            heat2d::Grid next(
                config.rows,
                config.cols,
                config.boundaryTemperature
            );

            // Execute the model and strictly capture pure compute time.
            const auto start =
                std::chrono::steady_clock::now();

            const heat2d::CoarseGrainedStats stats =
                heat2d::runCoarseGrained(
                    current,
                    next,
                    steps,
                    config.alpha,
                    config.dt,
                    config.dx(),
                    config.dy(),
                    config.boundaryTemperature,
                    coarseConfig
                );

            const auto end =
                std::chrono::steady_clock::now();

            const double elapsedSeconds =
                std::chrono::duration<double>(
                    end - start
                ).count();

            elapsedTimes.push_back(
                elapsedSeconds
            );

            // Metadata assertions ensure the thread pool didn't short-circuit.
            if (
                stats.executedCellUpdates !=
                expectedUpdates
            ) {
                throw std::runtime_error(
                    "Unexpected number of executed cell updates."
                );
            }

            if (
                stats.threadCount !=
                threadCount
            ) {
                throw std::runtime_error(
                    "Unexpected thread count in statistics."
                );
            }

            if (
                stats.timeSteps !=
                steps
            ) {
                throw std::runtime_error(
                    "Unexpected timestep count in statistics."
                );
            }

            if (
                stats.blockingEvents != 0
            ) {
                throw std::runtime_error(
                    "Blocking events reported while blocking is disabled."
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

            // Post-execution boundary integrity check.
            validateFiniteGrid(
                current
            );

            const double boundaryError =
                maximumBoundaryError(
                    current,
                    config.boundaryTemperature
                );

            if (boundaryError != 0.0) {
                throw std::runtime_error(
                    "Dirichlet boundary condition was modified."
                );
            }

            // Compare numerical reality against exact mathematical theory.
            const ErrorMetrics errors =
                calculateAnalyticalError(
                    current,
                    config,
                    simulatedTime
                );

            if (
                errors.l2 >
                kL2Tolerance
            ) {
                throw std::runtime_error(
                    "L2 error exceeds the stress-test tolerance."
                );
            }

            if (
                errors.lInf >
                kLInfTolerance
            ) {
                throw std::runtime_error(
                    "L-infinity error exceeds the stress-test tolerance."
                );
            }

            double determinismDifference =
                0.0;

            // Determinism verification.
            // Repetition 0 defines the mathematical standard. Repetition N must perfectly match.
            if (repetition == 0) {
                deterministicReference =
                    std::make_unique<
                        heat2d::Grid
                    >(
                        current
                    );

                referenceL2 =
                    errors.l2;

                referenceLInf =
                    errors.lInf;

            } else {
                determinismDifference =
                    maximumGridDifference(
                        *deterministicReference,
                        current
                    );

                // Any variance implies a synchronization bug.
                if (
                    determinismDifference != 0.0
                ) {
                    deterministic = false;
                }
            }

            const double throughput =
                static_cast<double>(
                    expectedUpdates
                ) /
                elapsedSeconds;

            std::cout
                << "Rep "
                << (repetition + 1)
                << ": "
                << elapsedSeconds
                << " s"
                << " | "
                << throughput / 1.0e6
                << " M updates/s"
                << " | L2 = "
                << errors.l2
                << " | Linf = "
                << errors.lInf;

            if (repetition > 0) {
                std::cout
                    << " | max diff vs ref = "
                    << determinismDifference;
            }

            std::cout
                << '\n';
        }

        // Aggregate statistics.
        double totalTime = 0.0;

        double minimumTime =
            elapsedTimes.front();

        double maximumTime =
            elapsedTimes.front();

        for (
            const double elapsed :
            elapsedTimes
        ) {
            totalTime += elapsed;

            minimumTime =
                std::min(
                    minimumTime,
                    elapsed
                );

            maximumTime =
                std::max(
                    maximumTime,
                    elapsed
                );
        }

        const double averageTime =
            totalTime /
            static_cast<double>(
                elapsedTimes.size()
            );

        const double averageThroughput =
            static_cast<double>(
                expectedUpdates
            ) /
            averageTime;

        std::cout
            << "\n--- Stress Summary ---\n";

        std::cout
            << "Reference L2 error: "
            << referenceL2
            << '\n';

        std::cout
            << "Reference Linf error: "
            << referenceLInf
            << '\n';

        std::cout
            << "L2 tolerance: "
            << kL2Tolerance
            << '\n';

        std::cout
            << "Linf tolerance: "
            << kLInfTolerance
            << '\n';

        std::cout
            << "Minimum time: "
            << minimumTime
            << " s\n";

        std::cout
            << "Maximum time: "
            << maximumTime
            << " s\n";

        std::cout
            << "Average time: "
            << averageTime
            << " s\n";

        std::cout
            << "Average throughput: "
            << averageThroughput / 1.0e6
            << " M updates/s\n";

        std::cout
            << "Finite values: PASS\n";

        std::cout
            << "Boundary condition: PASS\n";

        std::cout
            << "Analytical validation: PASS\n";

        std::cout
            << "Determinism: "
            << (deterministic ? "PASS" : "FAIL")
            << '\n';

        // Fail hard if the model wasn't deterministic.
        if (!deterministic) {
            throw std::runtime_error(
                "Coarse-Grained execution is not deterministic."
            );
        }

        std::cout
            << "\nCoarse-Grained stress test: PASSED\n\n";

        return 0;

    } catch (const std::exception& error) {
        std::cerr
            << "\nCoarse-Grained stress test: FAILED\n"
            << error.what()
            << '\n';

        return 1;
    }
}
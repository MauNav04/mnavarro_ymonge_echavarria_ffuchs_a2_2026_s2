#include "benchmark/benchmark.hpp"

#include "cmp/cmp.hpp"
#include "coarse_grained/coarse_grained.hpp"
#include "fine_grained/fine_grained.hpp"
#include "heat2d.hpp"
#include "initial_conditions.hpp"
#include "sequential.hpp"
#include "smt/smt.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

namespace heat2d::benchmark {

namespace {

/**
 * @brief Numerical tolerance used when comparing model outputs.
 *
 * A small tolerance is used to account for possible floating-point differences
 * while still detecting meaningful deviations between execution models.
 */
constexpr double validationTolerance =
    1e-12;


/**
 * @brief Multiplies two uint64 values while rigorously checking for overflow.
 *
 * Essential for accumulating statistics (like total cell updates) during 
 * massive benchmark campaigns running thousands of timesteps.
 *
 * @throws std::overflow_error If the product exceeds 64-bit bounds.
 */
std::uint64_t checkedMultiply64(
    std::uint64_t left,
    std::uint64_t right
) {
    if (
        left != 0 &&
        right >
            std::numeric_limits<std::uint64_t>::max() /
            left
    ) {
        throw std::overflow_error(
            "Benchmark statistics overflow."
        );
    }

    return left * right;
}


/**
 * @brief Validates the global physical and temporal configuration of the campaign.
 *
 * Asserts structural minimums (3x3 grid), valid positive physical dimensions, 
 * and rigorously enforces the Forward-Euler stability condition.
 * 
 * @throws std::invalid_argument If any configuration parameter is invalid or unstable.
 */
void validateBenchmarkConfig(
    const BenchmarkConfig& config
) {
    if (
        config.rows < 3 ||
        config.cols < 3
    ) {
        throw std::invalid_argument(
            "Benchmark grid must contain at least three rows "
            "and three columns."
        );
    }

    if (config.steps == 0) {
        throw std::invalid_argument(
            "Benchmark must execute at least one timestep."
        );
    }

    if (config.measuredRuns == 0) {
        throw std::invalid_argument(
            "Benchmark must contain at least one measured run."
        );
    }

    if (config.lengthX <= 0.0) {
        throw std::invalid_argument(
            "Benchmark lengthX must be greater than zero."
        );
    }

    if (config.lengthY <= 0.0) {
        throw std::invalid_argument(
            "Benchmark lengthY must be greater than zero."
        );
    }

    if (config.alpha <= 0.0) {
        throw std::invalid_argument(
            "Benchmark alpha must be greater than zero."
        );
    }

    if (config.dt <= 0.0) {
        throw std::invalid_argument(
            "Benchmark dt must be greater than zero."
        );
    }

    if (
        config.coarseBlockingDuration.count() < 0
    ) {
        throw std::invalid_argument(
            "Coarse-Grained blocking duration cannot be negative."
        );
    }

    if (
        config.coarseRowsPerBlockingEvent == 0
    ) {
        throw std::invalid_argument(
            "Coarse-Grained rows per blocking event "
            "must be greater than zero."
        );
    }

    const double dx =
        config.lengthX /
        static_cast<double>(
            config.cols - 1
        );

    const double dy =
        config.lengthY /
        static_cast<double>(
            config.rows - 1
        );

    if (
        !isStable(
            config.alpha,
            config.dt,
            dx,
            dy
        )
    ) {
        throw std::invalid_argument(
            "Benchmark numerical configuration is unstable."
        );
    }
}


/**
 * @brief Validates the logical constraints of an individual execution case.
 * 
 * Ensures that the requested thread count aligns with the capabilities and 
 * definitions of the selected concurrency model (e.g., Sequential must equal 1).
 */
void validateBenchmarkCase(
    const BenchmarkCase& benchmarkCase
) {
    switch (benchmarkCase.model) {
        case ExecutionModel::Sequential:
            if (
                benchmarkCase.logicalThreadCount != 1
            ) {
                throw std::invalid_argument(
                    "Sequential benchmark must use one execution context."
                );
            }

            return;

        case ExecutionModel::FineGrained:
            if (
                benchmarkCase.logicalThreadCount == 0
            ) {
                throw std::invalid_argument(
                    "Fine-Grained benchmark requires at least one "
                    "logical thread."
                );
            }

            return;

        case ExecutionModel::CoarseGrained:
            if (
                benchmarkCase.logicalThreadCount == 0
            ) {
                throw std::invalid_argument(
                    "Coarse-Grained benchmark requires at least one "
                    "worker thread."
                );
            }

            return;

        case ExecutionModel::Cmp:
            if (
                benchmarkCase.logicalThreadCount == 0
            ) {
                throw std::invalid_argument(
                    "CMP benchmark requires at least one "
                    "worker thread."
                );
            }

            return;

        case ExecutionModel::Smt:
            if (
                benchmarkCase.logicalThreadCount == 0
            ) {
                throw std::invalid_argument(
                    "SMT benchmark requires at least one "
                    "hardware worker thread."
                );
            }

            return;
    }

    throw std::invalid_argument(
        "Benchmark contains an unsupported execution model."
    );
}


/**
 * @brief Iterates over and validates an entire suite of execution cases.
 */
void validateCases(
    const std::vector<BenchmarkCase>& cases
) {
    if (cases.empty()) {
        throw std::invalid_argument(
            "Benchmark requires at least one execution case."
        );
    }

    for (
        const BenchmarkCase& benchmarkCase :
        cases
    ) {
        validateBenchmarkCase(
            benchmarkCase
        );
    }
}


/**
 * @brief Calculates the horizontal spatial step (dx).
 */
double calculateDx(
    const BenchmarkConfig& config
) {
    return
        config.lengthX /
        static_cast<double>(
            config.cols - 1
        );
}


/**
 * @brief Calculates the vertical spatial step (dy).
 */
double calculateDy(
    const BenchmarkConfig& config
) {
    return
        config.lengthY /
        static_cast<double>(
            config.rows - 1
        );
}


/**
 * @brief Constructs the baseline grid state for the benchmark suite.
 * 
 * Generates the sinusoidal thermal distribution once. This exact grid is 
 * copied repeatedly into every model run to guarantee absolute uniformity 
 * across the experimental campaign.
 */
Grid createInitialGrid(
    const BenchmarkConfig& config
) {
    Grid grid(
        config.rows,
        config.cols,
        config.boundaryTemperature
    );

    initializeSinusoidal(
        grid,
        config.maxTemperature,
        config.lengthX,
        config.lengthY
    );

    applyFixedBoundary(
        grid,
        config.boundaryTemperature
    );

    return grid;
}


/**
 * @brief Instantiates the specific execution parameters for the Coarse-Grained solver.
 */
heat2d::CoarseGrainedConfig createCoarseGrainedConfig(
    const BenchmarkConfig& config,
    const BenchmarkCase& benchmarkCase
) {
    heat2d::CoarseGrainedConfig coarseConfig;

    coarseConfig.threadCount =
        benchmarkCase.logicalThreadCount;

    coarseConfig.blockingDuration =
        config.coarseBlockingDuration;

    coarseConfig.rowsPerBlockingEvent =
        config.coarseRowsPerBlockingEvent;

    return coarseConfig;
}


/**
 * @brief Instantiates the specific execution parameters for the CMP solver.
 */
heat2d::CmpConfig createCmpConfig(
    const BenchmarkCase& benchmarkCase
) {
    heat2d::CmpConfig cmpConfig;

    cmpConfig.threadCount =
        benchmarkCase.logicalThreadCount;

    return cmpConfig;
}


/**
 * @brief Instantiates the specific execution parameters for the SMT solver.
 */
heat2d::smt::SmtConfig createSmtConfig(
    const BenchmarkCase& benchmarkCase
) {
    heat2d::smt::SmtConfig smtConfig;

    smtConfig.threadCount =
        benchmarkCase.logicalThreadCount;

    return smtConfig;
}


/**
 * @brief Mathematically determines the exact expected number of injected stalls.
 */
std::uint64_t calculateExpectedBlockingEvents(
    const BenchmarkConfig& config
) {
    if (
        config.coarseBlockingDuration.count() == 0
    ) {
        return 0;
    }

    const std::uint64_t interiorRows =
        static_cast<std::uint64_t>(
            config.rows - 2
        );

    const std::uint64_t rowsPerEvent =
        static_cast<std::uint64_t>(
            config.coarseRowsPerBlockingEvent
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

    return checkedMultiply64(
        eventsPerStep,
        static_cast<std::uint64_t>(
            config.steps
        )
    );
}


/**
 * @brief Asserts the returned statistics from the Fine-Grained execution.
 *
 * Ensures no quanta (cell updates) were dropped or duplicated by the 
 * Round-Robin scheduler. This validation occurs *outside* the timed region 
 * to prevent instrumentation overhead from spoiling the benchmark data.
 */
void validateFineGrainedStats(
    const fine_grained::FineGrainedStats& stats,
    const BenchmarkConfig& config,
    const BenchmarkCase& benchmarkCase
) {
    if (
        stats.logicalThreadCount !=
        benchmarkCase.logicalThreadCount
    ) {
        throw std::runtime_error(
            "Fine-Grained benchmark returned an incorrect "
            "logical-thread count."
        );
    }

    if (
        stats.timeSteps !=
        config.steps
    ) {
        throw std::runtime_error(
            "Fine-Grained benchmark returned an incorrect "
            "timestep count."
        );
    }

    if (
        stats.executedQuanta !=
        calculateCellUpdates(config)
    ) {
        throw std::runtime_error(
            "Fine-Grained benchmark returned an incorrect "
            "quantum count."
        );
    }
}


/**
 * @brief Asserts the returned statistics from the Coarse-Grained execution.
 *
 * Validates thread count, executed cell updates, blocking events, and 
 * synchronization barriers. Handled outside the timed region.
 */
void validateCoarseGrainedStats(
    const heat2d::CoarseGrainedStats& stats,
    const BenchmarkConfig& config,
    const BenchmarkCase& benchmarkCase
) {
    if (
        stats.threadCount !=
        benchmarkCase.logicalThreadCount
    ) {
        throw std::runtime_error(
            "Coarse-Grained benchmark returned an incorrect "
            "worker-thread count."
        );
    }

    if (
        stats.timeSteps !=
        config.steps
    ) {
        throw std::runtime_error(
            "Coarse-Grained benchmark returned an incorrect "
            "timestep count."
        );
    }

    const std::uint64_t expectedCellUpdates =
        static_cast<std::uint64_t>(
            calculateCellUpdates(
                config
            )
        );

    if (
        stats.executedCellUpdates !=
        expectedCellUpdates
    ) {
        throw std::runtime_error(
            "Coarse-Grained benchmark returned an incorrect "
            "cell-update count."
        );
    }

    const std::uint64_t expectedBlockingEvents =
        calculateExpectedBlockingEvents(
            config
        );

    if (
        stats.blockingEvents !=
        expectedBlockingEvents
    ) {
        throw std::runtime_error(
            "Coarse-Grained benchmark returned an incorrect "
            "blocking-event count."
        );
    }

    const std::uint64_t expectedBarrierPhases =
        checkedMultiply64(
            2ULL,
            static_cast<std::uint64_t>(
                config.steps
            )
        );

    if (
        stats.barrierPhases !=
        expectedBarrierPhases
    ) {
        throw std::runtime_error(
            "Coarse-Grained benchmark returned an incorrect "
            "barrier-phase count."
        );
    }
}


/**
 * @brief Asserts the returned statistics from the CMP execution.
 *
 * Validates thread count, cell updates, and dual-phase barriers. 
 * Handled outside the timed region.
 */
void validateCmpStats(
    const heat2d::CmpStats& stats,
    const BenchmarkConfig& config,
    const BenchmarkCase& benchmarkCase
) {
    if (
        stats.threadCount !=
        benchmarkCase.logicalThreadCount
    ) {
        throw std::runtime_error(
            "CMP benchmark returned an incorrect "
            "worker-thread count."
        );
    }

    if (
        stats.timeSteps !=
        config.steps
    ) {
        throw std::runtime_error(
            "CMP benchmark returned an incorrect "
            "timestep count."
        );
    }

    const std::uint64_t expectedCellUpdates =
        static_cast<std::uint64_t>(
            calculateCellUpdates(
                config
            )
        );

    if (
        stats.executedCellUpdates !=
        expectedCellUpdates
    ) {
        throw std::runtime_error(
            "CMP benchmark returned an incorrect "
            "cell-update count."
        );
    }

    const std::uint64_t expectedBarrierPhases =
        checkedMultiply64(
            2ULL,
            static_cast<std::uint64_t>(
                config.steps
            )
        );

    if (
        stats.barrierPhases !=
        expectedBarrierPhases
    ) {
        throw std::runtime_error(
            "CMP benchmark returned an incorrect "
            "barrier-phase count."
        );
    }
}


/**
 * @brief Asserts the returned statistics from the SMT execution.
 *
 * Validates threads, topological CPU constraints, thread-pinning behavior, 
 * and computational invariants. Handled outside the timed region.
 */
void validateSmtStats(
    const heat2d::smt::SmtStats& stats,
    const BenchmarkConfig& config,
    const BenchmarkCase& benchmarkCase
) {
    if (
        stats.threadCount !=
        benchmarkCase.logicalThreadCount
    ) {
        throw std::runtime_error(
            "SMT benchmark returned an incorrect worker-thread count."
        );
    }

    if (
        stats.timeSteps !=
        config.steps
    ) {
        throw std::runtime_error(
            "SMT benchmark returned an incorrect timestep count."
        );
    }

    const std::uint64_t expectedCellUpdates =
        static_cast<std::uint64_t>(
            calculateCellUpdates(
                config
            )
        );

    if (
        stats.executedCellUpdates !=
        expectedCellUpdates
    ) {
        throw std::runtime_error(
            "SMT benchmark returned an incorrect cell-update count."
        );
    }

    const std::uint64_t expectedBarrierPhases =
        checkedMultiply64(
            2ULL,
            static_cast<std::uint64_t>(
                config.steps
            )
        );

    if (
        stats.barrierPhases !=
        expectedBarrierPhases
    ) {
        throw std::runtime_error(
            "SMT benchmark returned an incorrect barrier-phase count."
        );
    }

    if (
        stats.pinnedWorkerCount !=
        benchmarkCase.logicalThreadCount
    ) {
        throw std::runtime_error(
            "SMT benchmark did not pin every worker thread."
        );
    }

    if (
        stats.availableLogicalCpus <
        benchmarkCase.logicalThreadCount
    ) {
        throw std::runtime_error(
            "SMT benchmark used more workers than available logical CPUs."
        );
    }

    if (
        stats.logicalCpuIds.size() !=
        benchmarkCase.logicalThreadCount
    ) {
        throw std::runtime_error(
            "SMT benchmark returned an incorrect CPU-assignment count."
        );
    }

    const std::set<int> uniqueCpuIds(
        stats.logicalCpuIds.begin(),
        stats.logicalCpuIds.end()
    );

    if (
        uniqueCpuIds.size() !=
        stats.logicalCpuIds.size()
    ) {
        throw std::runtime_error(
            "SMT benchmark assigned multiple workers to the same logical CPU."
        );
    }
}


/**
 * @brief Executes a single benchmark model without invoking the chronometer.
 *
 * This function is used exclusively during the pre-campaign numerical 
 * validation phase to ensure all execution models produce identical heat 
 * maps before the actual timed runs begin.
 */
Grid executeUnmeasured(
    const BenchmarkConfig& config,
    const BenchmarkCase& benchmarkCase,
    const Grid& initialGrid
) {
    Grid current =
        initialGrid;

    Grid next(
        config.rows,
        config.cols,
        config.boundaryTemperature
    );

    const double dx =
        calculateDx(
            config
        );

    const double dy =
        calculateDy(
            config
        );

    switch (benchmarkCase.model) {
        case ExecutionModel::Sequential:
            runSequential(
                current,
                next,
                config.steps,
                config.alpha,
                config.dt,
                dx,
                dy,
                config.boundaryTemperature
            );

            break;

        case ExecutionModel::FineGrained: {
            const fine_grained::FineGrainedStats stats =
                fine_grained::runFineGrained(
                    current,
                    next,
                    config.steps,
                    benchmarkCase.logicalThreadCount,
                    config.alpha,
                    config.dt,
                    dx,
                    dy,
                    config.boundaryTemperature
                );

            validateFineGrainedStats(
                stats,
                config,
                benchmarkCase
            );

            break;
        }

        case ExecutionModel::CoarseGrained: {
            const heat2d::CoarseGrainedConfig coarseConfig =
                createCoarseGrainedConfig(
                    config,
                    benchmarkCase
                );

            const heat2d::CoarseGrainedStats stats =
                heat2d::runCoarseGrained(
                    current,
                    next,
                    config.steps,
                    config.alpha,
                    config.dt,
                    dx,
                    dy,
                    config.boundaryTemperature,
                    coarseConfig
                );

            validateCoarseGrainedStats(
                stats,
                config,
                benchmarkCase
            );

            break;
        }

        case ExecutionModel::Cmp: {
            const heat2d::CmpConfig cmpConfig =
                createCmpConfig(
                    benchmarkCase
                );

            const heat2d::CmpStats stats =
                heat2d::runCmp(
                    current,
                    next,
                    config.steps,
                    config.alpha,
                    config.dt,
                    dx,
                    dy,
                    config.boundaryTemperature,
                    cmpConfig
                );

            validateCmpStats(
                stats,
                config,
                benchmarkCase
            );

            break;
        }

        case ExecutionModel::Smt: {
            const heat2d::smt::SmtConfig smtConfig =
                createSmtConfig(
                    benchmarkCase
                );

            const heat2d::smt::SmtStats stats =
                heat2d::smt::runSmt(
                    current,
                    next,
                    config.steps,
                    config.alpha,
                    config.dt,
                    dx,
                    dy,
                    config.boundaryTemperature,
                    smtConfig
                );

            validateSmtStats(
                stats,
                config,
                benchmarkCase
            );

            break;
        }
    }

    return current;
}


/**
 * @brief Computes the largest absolute scalar difference between two grids.
 * 
 * Used during pre-campaign validation to identify non-deterministic race 
 * conditions or synchronization failures across the execution models.
 * 
 * @throws std::invalid_argument If the two grid topologies differ.
 */
double calculateMaximumDifference(
    const Grid& first,
    const Grid& second
) {
    if (
        first.rows() != second.rows() ||
        first.cols() != second.cols()
    ) {
        throw std::invalid_argument(
            "Cannot compare benchmark grids with different dimensions."
        );
    }

    double maximumDifference =
        0.0;

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
 * @brief Measures the execution time of a single benchmark observation.
 *
 * Designed to guarantee scientific accuracy: grid creation, spatial parameter 
 * calculation, model configuration objects, and validation metadata assertions 
 * are strictly hoisted outside the `start` and `end` chronometer boundary.
 */
BenchmarkSample executeMeasured(
    const BenchmarkConfig& config,
    const BenchmarkCase& benchmarkCase,
    const Grid& initialGrid,
    std::size_t run
) {
    Grid current =
        initialGrid;

    Grid next(
        config.rows,
        config.cols,
        config.boundaryTemperature
    );

    /*
     * Spatial parameters are prepared before starting the timer.
     */
    const double dx =
        calculateDx(
            config
        );

    const double dy =
        calculateDy(
            config
        );

    fine_grained::FineGrainedStats fineGrainedStats{
        0,
        0,
        0
    };

    heat2d::CoarseGrainedStats coarseGrainedStats{};

    heat2d::CmpStats cmpStats{};

    heat2d::smt::SmtStats smtStats{};

    bool hasFineGrainedStats =
        false;

    bool hasCoarseGrainedStats =
        false;

    bool hasCmpStats =
        false;

    bool hasSmtStats =
        false;

    /*
     * The lightweight Coarse-Grained configuration object is also
     * prepared outside the timed region.
     */
    heat2d::CoarseGrainedConfig coarseConfig;

    if (
        benchmarkCase.model ==
        ExecutionModel::CoarseGrained
    ) {
        coarseConfig =
            createCoarseGrainedConfig(
                config,
                benchmarkCase
            );
    }

    /*
     * The lightweight CMP configuration object is also prepared
     * outside the timed region.
     */
    heat2d::CmpConfig cmpConfig;

    if (
        benchmarkCase.model ==
        ExecutionModel::Cmp
    ) {
        cmpConfig =
            createCmpConfig(
                benchmarkCase
            );
    }

    heat2d::smt::SmtConfig smtConfig;

    if (
        benchmarkCase.model ==
        ExecutionModel::Smt
    ) {
        smtConfig =
            createSmtConfig(
                benchmarkCase
            );
    }

    // === START CRITICAL TIMING SECTION ===
    const auto start =
        std::chrono::steady_clock::now();

    switch (benchmarkCase.model) {
        case ExecutionModel::Sequential:
            runSequential(
                current,
                next,
                config.steps,
                config.alpha,
                config.dt,
                dx,
                dy,
                config.boundaryTemperature
            );

            break;

        case ExecutionModel::FineGrained:
            fineGrainedStats =
                fine_grained::runFineGrained(
                    current,
                    next,
                    config.steps,
                    benchmarkCase.logicalThreadCount,
                    config.alpha,
                    config.dt,
                    dx,
                    dy,
                    config.boundaryTemperature
                );

            hasFineGrainedStats =
                true;

            break;

        case ExecutionModel::CoarseGrained:
            coarseGrainedStats =
                heat2d::runCoarseGrained(
                    current,
                    next,
                    config.steps,
                    config.alpha,
                    config.dt,
                    dx,
                    dy,
                    config.boundaryTemperature,
                    coarseConfig
                );

            hasCoarseGrainedStats =
                true;

            break;

        case ExecutionModel::Cmp:
            cmpStats =
                heat2d::runCmp(
                    current,
                    next,
                    config.steps,
                    config.alpha,
                    config.dt,
                    dx,
                    dy,
                    config.boundaryTemperature,
                    cmpConfig
                );

            hasCmpStats =
                true;

            break;

        case ExecutionModel::Smt:
            smtStats =
                heat2d::smt::runSmt(
                    current,
                    next,
                    config.steps,
                    config.alpha,
                    config.dt,
                    dx,
                    dy,
                    config.boundaryTemperature,
                    smtConfig
                );

            hasSmtStats =
                true;

            break;
    }

    const auto end =
        std::chrono::steady_clock::now();
    // === END CRITICAL TIMING SECTION ===

    /*
     * Metadata validation happens after the timer stops.
     */
    if (hasFineGrainedStats) {
        validateFineGrainedStats(
            fineGrainedStats,
            config,
            benchmarkCase
        );
    }

    if (hasCoarseGrainedStats) {
        validateCoarseGrainedStats(
            coarseGrainedStats,
            config,
            benchmarkCase
        );
    }

    if (hasCmpStats) {
        validateCmpStats(
            cmpStats,
            config,
            benchmarkCase
        );
    }

    if (hasSmtStats) {
        validateSmtStats(
            smtStats,
            config,
            benchmarkCase
        );
    }

    const double elapsedSeconds =
        std::chrono::duration<double>(
            end - start
        ).count();

    // Validate that the measured duration is finite and strictly positive.
    if (
        elapsedSeconds <= 0.0 ||
        !std::isfinite(
            elapsedSeconds
        )
    ) {
        throw std::runtime_error(
            "Benchmark produced an invalid execution time."
        );
    }

    const std::size_t cellUpdates =
        calculateCellUpdates(
            config
        );

    const double throughput =
        static_cast<double>(
            cellUpdates
        ) /
        elapsedSeconds;

    /*
     * Cheap post-execution sanity check to ensure floating-point values 
     * haven't cascaded into infinity/NaNs during the execution.
     */
    const double centerTemperature =
        current(
            config.rows / 2,
            config.cols / 2
        );

    if (
        !std::isfinite(
            centerTemperature
        )
    ) {
        throw std::runtime_error(
            "Benchmark produced a non-finite final temperature."
        );
    }

    return {
        run,
        benchmarkCase.model,
        benchmarkCase.logicalThreadCount,
        config.rows,
        config.cols,
        config.steps,
        config.alpha,
        config.dt,
        cellUpdates,
        elapsedSeconds,
        throughput
    };
}


/**
 * @brief Executes a single warmup round intended to prime CPU caches and frequency scaling.
 * 
 * Invokes the model execution but discards the time output.
 */
void executeWarmup(
    const BenchmarkConfig& config,
    const BenchmarkCase& benchmarkCase,
    const Grid& initialGrid
) {
    Grid current =
        initialGrid;

    Grid next(
        config.rows,
        config.cols,
        config.boundaryTemperature
    );

    const double dx =
        calculateDx(
            config
        );

    const double dy =
        calculateDy(
            config
        );

    switch (benchmarkCase.model) {
        case ExecutionModel::Sequential:
            runSequential(
                current,
                next,
                config.steps,
                config.alpha,
                config.dt,
                dx,
                dy,
                config.boundaryTemperature
            );

            break;

        case ExecutionModel::FineGrained: {
            const fine_grained::FineGrainedStats stats =
                fine_grained::runFineGrained(
                    current,
                    next,
                    config.steps,
                    benchmarkCase.logicalThreadCount,
                    config.alpha,
                    config.dt,
                    dx,
                    dy,
                    config.boundaryTemperature
                );

            validateFineGrainedStats(
                stats,
                config,
                benchmarkCase
            );

            break;
        }

        case ExecutionModel::CoarseGrained: {
            const heat2d::CoarseGrainedConfig coarseConfig =
                createCoarseGrainedConfig(
                    config,
                    benchmarkCase
                );

            const heat2d::CoarseGrainedStats stats =
                heat2d::runCoarseGrained(
                    current,
                    next,
                    config.steps,
                    config.alpha,
                    config.dt,
                    dx,
                    dy,
                    config.boundaryTemperature,
                    coarseConfig
                );

            validateCoarseGrainedStats(
                stats,
                config,
                benchmarkCase
            );

            break;
        }

        case ExecutionModel::Cmp: {
            const heat2d::CmpConfig cmpConfig =
                createCmpConfig(
                    benchmarkCase
                );

            const heat2d::CmpStats stats =
                heat2d::runCmp(
                    current,
                    next,
                    config.steps,
                    config.alpha,
                    config.dt,
                    dx,
                    dy,
                    config.boundaryTemperature,
                    cmpConfig
                );

            validateCmpStats(
                stats,
                config,
                benchmarkCase
            );

            break;
        }

        case ExecutionModel::Smt: {
            const heat2d::smt::SmtConfig smtConfig =
                createSmtConfig(
                    benchmarkCase
                );

            const heat2d::smt::SmtStats stats =
                heat2d::smt::runSmt(
                    current,
                    next,
                    config.steps,
                    config.alpha,
                    config.dt,
                    dx,
                    dy,
                    config.boundaryTemperature,
                    smtConfig
                );

            validateSmtStats(
                stats,
                config,
                benchmarkCase
            );

            break;
        }
    }
}

}  // namespace


/**
 * @brief Computes the absolute maximum cell updates executed by a single run.
 */
std::size_t calculateCellUpdates(
    const BenchmarkConfig& config
) {
    if (
        config.rows < 3 ||
        config.cols < 3
    ) {
        throw std::invalid_argument(
            "Benchmark grid must contain at least three rows "
            "and three columns."
        );
    }

    const std::size_t interiorRows =
        config.rows - 2;

    const std::size_t interiorColumns =
        config.cols - 2;

    if (
        interiorRows != 0 &&
        interiorColumns >
            std::numeric_limits<std::size_t>::max() /
            interiorRows
    ) {
        throw std::overflow_error(
            "Benchmark interior-cell count exceeds "
            "std::size_t capacity."
        );
    }

    const std::size_t interiorCells =
        interiorRows *
        interiorColumns;

    if (
        interiorCells != 0 &&
        config.steps >
            std::numeric_limits<std::size_t>::max() /
            interiorCells
    ) {
        throw std::overflow_error(
            "Benchmark cell-update count exceeds "
            "std::size_t capacity."
        );
    }

    return
        interiorCells *
        config.steps;
}


/**
 * @brief Translates an ExecutionModel enumerator into its standardized string literal.
 */
const char* modelName(
    ExecutionModel model
) noexcept {
    switch (model) {
        case ExecutionModel::Sequential:
            return "sequential";

        case ExecutionModel::FineGrained:
            return "fine_grained";

        case ExecutionModel::CoarseGrained:
            return "coarse_grained";

        case ExecutionModel::Cmp:
            return "cmp";

        case ExecutionModel::Smt:
            return "smt";
    }

    return "unknown";
}


/**
 * @brief Validates the logical correctness of every target model prior to benchmarking.
 *
 * Runs each provided `BenchmarkCase` using the given `BenchmarkConfig` without 
 * measuring the execution time. Then computes the delta against the Serial implementation. 
 * If any concurrency bug (e.g., race conditions, phase bleeding) causes the grid state 
 * to drift beyond `validationTolerance`, it fails the suite upfront.
 * 
 * @throws std::runtime_error If the max difference exceeds the permitted delta.
 */
void validateBenchmarkCases(
    const BenchmarkConfig& config,
    const std::vector<BenchmarkCase>& cases
) {
    validateBenchmarkConfig(
        config
    );

    validateCases(
        cases
    );

    const Grid initialGrid =
        createInitialGrid(
            config
        );

    /*
     * The sequential model defines the numerical reference.
     */
    const BenchmarkCase sequentialCase{
        ExecutionModel::Sequential,
        1
    };

    const Grid reference =
        executeUnmeasured(
            config,
            sequentialCase,
            initialGrid
        );

    for (
        const BenchmarkCase& benchmarkCase :
        cases
    ) {
        const Grid candidate =
            executeUnmeasured(
                config,
                benchmarkCase,
                initialGrid
            );

        const double maximumDifference =
            calculateMaximumDifference(
                reference,
                candidate
            );

        if (
            maximumDifference >
            validationTolerance
        ) {
            throw std::runtime_error(
                "Benchmark case failed numerical validation "
                "against the sequential reference."
            );
        }
    }
}


/**
 * @brief Primes the system by running discardable warm-up phases.
 *
 * Warm-up executions help populate caches, map memory pages, and reduce
 * transient effects associated with CPU frequency scaling before measurements.
 */
void runWarmups(
    const BenchmarkConfig& config,
    const std::vector<BenchmarkCase>& cases
) {
    validateBenchmarkConfig(
        config
    );

    validateCases(
        cases
    );

    const Grid initialGrid =
        createInitialGrid(
            config
        );

    for (
        std::size_t round = 0;
        round < config.warmupRuns;
        ++round
    ) {
        /*
         * Warm-up order follows the same rotation policy used
         * during measured rounds.
         */
        for (
            std::size_t offset = 0;
            offset < cases.size();
            ++offset
        ) {
            const std::size_t index =
                (
                    round +
                    offset
                ) %
                cases.size();

            executeWarmup(
                config,
                cases[index],
                initialGrid
            );
        }
    }
}


/**
 * @brief Main execution orchestrator for an automated benchmarking campaign.
 *
 * Runs `measuredRuns` cycles of all provided `cases`. Crucially, it applies 
 * a rotational offset per round to prevent any given model from continuously 
 * executing first or last, mitigating systemic thermal throttling bias.
 * 
 * @return std::vector<BenchmarkSample> A flattened array containing precisely 
 *         timed execution statistics for every test permutation.
 */
std::vector<BenchmarkSample> runBenchmark(
    const BenchmarkConfig& config,
    const std::vector<BenchmarkCase>& cases
) {
    validateBenchmarkConfig(
        config
    );

    validateCases(
        cases
    );

    const Grid initialGrid =
        createInitialGrid(
            config
        );

    if (
        cases.size() != 0 &&
        config.measuredRuns >
            std::numeric_limits<std::size_t>::max() /
            cases.size()
    ) {
        throw std::overflow_error(
            "Benchmark sample count exceeds std::size_t capacity."
        );
    }

    const std::size_t sampleCount =
        cases.size() *
        config.measuredRuns;

    std::vector<BenchmarkSample> samples;

    samples.reserve(
        sampleCount
    );

    for (
        std::size_t round = 0;
        round < config.measuredRuns;
        ++round
    ) {
        /*
         * Rotate the first configuration every round.
         * Mitigates thermal-bias (where the first test always gets a cool CPU).
         *
         * R1: A B C D
         * R2: B C D A
         * R3: C D A B
         */
        for (
            std::size_t offset = 0;
            offset < cases.size();
            ++offset
        ) {
            const std::size_t index =
                (
                    round +
                    offset
                ) %
                cases.size();

            samples.push_back(
                executeMeasured(
                    config,
                    cases[index],
                    initialGrid,
                    round + 1
                )
            );
        }
    }

    return samples;
}

}  // namespace heat2d::benchmark
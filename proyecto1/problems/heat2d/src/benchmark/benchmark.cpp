#include "benchmark/benchmark.hpp"

#include "cmp/cmp.hpp"
#include "coarse_grained/coarse_grained.hpp"
#include "fine_grained/fine_grained.hpp"
#include "heat2d.hpp"
#include "initial_conditions.hpp"
#include "sequential.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace heat2d::benchmark {

namespace {

constexpr double validationTolerance =
    1e-12;


/**
 * @brief Multiplies two uint64 values while checking overflow.
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
 * @brief Validates the numerical and experimental benchmark configuration.
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
 * @brief Validates one execution case.
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
    }

    throw std::invalid_argument(
        "Benchmark contains an unsupported execution model."
    );
}


/**
 * @brief Validates that the benchmark has usable execution cases.
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
 * @brief Returns the spatial step in the X direction.
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
 * @brief Returns the spatial step in the Y direction.
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
 * @brief Creates the common initial state used by every benchmark run.
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
 * @brief Builds the configuration used by one Coarse-Grained case.
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
 * @brief Builds the configuration used by one CMP case.
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
 * @brief Calculates the expected number of synthetic blocking events.
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
 * @brief Validates the metadata returned by the Fine-Grained solver.
 *
 * This check is intentionally performed outside timed regions.
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
 * @brief Validates the metadata returned by the Coarse-Grained solver.
 *
 * This check is intentionally performed outside timed regions.
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
 * @brief Validates the metadata returned by the CMP solver.
 *
 * This check is intentionally performed outside timed regions.
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
 * @brief Executes one model without measuring it.
 *
 * Used exclusively for correctness validation.
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
    }

    return current;
}


/**
 * @brief Calculates the maximum absolute difference between two grids.
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
 * @brief Executes one timed benchmark observation.
 *
 * Grid creation, initialization and post-execution validation
 * are deliberately excluded from the timed region.
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

    bool hasFineGrainedStats =
        false;

    bool hasCoarseGrainedStats =
        false;

    bool hasCmpStats =
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
    }

    const auto end =
        std::chrono::steady_clock::now();

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

    const double elapsedSeconds =
        std::chrono::duration<double>(
            end - start
        ).count();

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
     * Cheap post-execution sanity check.
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
 * @brief Executes one discarded warm-up.
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
    }
}

}  // namespace


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
    }

    return "unknown";
}


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
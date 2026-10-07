#pragma once

#include "grid.hpp"

#include <cstddef>
#include <vector>

namespace heat2d::benchmark {

enum class ExecutionModel {
    Sequential,
    FineGrained
};


/**
 * @brief Numerical and experimental configuration shared by all models.
 */
struct BenchmarkConfig {
    double lengthX = 1.0;
    double lengthY = 1.0;

    std::size_t rows = 801;
    std::size_t cols = 801;

    double alpha = 0.05;
    double dt = 0.000005;

    std::size_t steps = 1000;

    double maxTemperature = 100.0;
    double boundaryTemperature = 0.0;

    std::size_t warmupRuns = 1;
    std::size_t measuredRuns = 5;
};


/**
 * @brief One execution configuration participating in the experiment.
 */
struct BenchmarkCase {
    ExecutionModel model;
    std::size_t logicalThreadCount;
};


/**
 * @brief One raw measured observation.
 */
struct BenchmarkSample {
    std::size_t run;

    ExecutionModel model;

    std::size_t logicalThreadCount;

    std::size_t rows;
    std::size_t cols;
    std::size_t steps;

    double alpha;
    double dt;

    std::size_t cellUpdates;

    double elapsedSeconds;

    double cellUpdatesPerSecond;
};


[[nodiscard]]
std::size_t calculateCellUpdates(
    const BenchmarkConfig& config
);


[[nodiscard]]
const char* modelName(
    ExecutionModel model
) noexcept;


/**
 * @brief Validates all benchmark cases against a sequential reference.
 *
 * This verification is performed before timing begins.
 */
void validateBenchmarkCases(
    const BenchmarkConfig& config,
    const std::vector<BenchmarkCase>& cases
);


/**
 * @brief Executes discarded warm-up rounds.
 */
void runWarmups(
    const BenchmarkConfig& config,
    const std::vector<BenchmarkCase>& cases
);


/**
 * @brief Executes measured rounds and returns every raw sample.
 *
 * Cases are rotated between rounds to reduce systematic ordering bias.
 */
[[nodiscard]]
std::vector<BenchmarkSample> runBenchmark(
    const BenchmarkConfig& config,
    const std::vector<BenchmarkCase>& cases
);

}  // namespace heat2d::benchmark
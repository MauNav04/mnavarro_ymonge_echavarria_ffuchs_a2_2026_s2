#include "benchmark/benchmark.hpp"

#include <chrono>
#include <cstddef>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

std::size_t parsePositiveInteger(
    const char* argument,
    const char* name
) {
    const std::string value(
        argument
    );

    if (
        value.empty() ||
        value.front() == '-'
    ) {
        throw std::invalid_argument(
            std::string(name) +
            " must be a positive integer."
        );
    }

    std::size_t processedCharacters =
        0;

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

    return static_cast<std::size_t>(
        parsed
    );
}


void writeCsv(
    const std::string& outputPath,
    const std::vector<
        heat2d::benchmark::BenchmarkSample
    >& samples
) {
    std::ofstream output(
        outputPath
    );

    if (!output) {
        throw std::runtime_error(
            "Unable to open benchmark output file."
        );
    }

    /*
     * logical_threads is temporarily preserved for compatibility
     * with the existing analysis scripts.
     *
     * For Coarse-Grained it represents real worker threads.
     */
    output
        << "run,"
        << "model,"
        << "logical_threads,"
        << "rows,"
        << "cols,"
        << "steps,"
        << "alpha,"
        << "dt,"
        << "cell_updates,"
        << "elapsed_seconds,"
        << "cell_updates_per_second\n";

    output
        << std::setprecision(
            17
        );

    for (
        const auto& sample :
        samples
    ) {
        output
            << sample.run
            << ','
            << heat2d::benchmark::modelName(
                sample.model
            )
            << ','
            << sample.logicalThreadCount
            << ','
            << sample.rows
            << ','
            << sample.cols
            << ','
            << sample.steps
            << ','
            << sample.alpha
            << ','
            << sample.dt
            << ','
            << sample.cellUpdates
            << ','
            << sample.elapsedSeconds
            << ','
            << sample.cellUpdatesPerSecond
            << '\n';
    }
}

}  // namespace


int main(
    int argc,
    char* argv[]
) {
    try {
        if (argc > 4) {
            throw std::invalid_argument(
                "Usage: ./heat2d_benchmark "
                "[measured_runs] "
                "[warmup_runs] "
                "[output.csv]"
            );
        }

        heat2d::benchmark::BenchmarkConfig config;

        if (argc >= 2) {
            config.measuredRuns =
                parsePositiveInteger(
                    argv[1],
                    "Measured runs"
                );
        }

        if (argc >= 3) {
            config.warmupRuns =
                parsePositiveInteger(
                    argv[2],
                    "Warm-up runs"
                );
        }

        const std::string outputPath =
            argc >= 4
                ? argv[3]
                : "heat2d_benchmark.csv";

        /*
         * Common experimental matrix.
         *
         * Sequential:
         *   1 reference configuration.
         *
         * Fine-Grained:
         *   1, 2, 4, 8, 16, 32, 64 logical contexts.
         *
         * Coarse-Grained:
         *   1, 2, 4, 8, 16, 32, 64 real worker threads.
         */
        const std::vector<
            heat2d::benchmark::BenchmarkCase
        > cases{
            {
                heat2d::benchmark::ExecutionModel::Sequential,
                1
            },

            {
                heat2d::benchmark::ExecutionModel::FineGrained,
                1
            },
            {
                heat2d::benchmark::ExecutionModel::FineGrained,
                2
            },
            {
                heat2d::benchmark::ExecutionModel::FineGrained,
                4
            },
            {
                heat2d::benchmark::ExecutionModel::FineGrained,
                8
            },
            {
                heat2d::benchmark::ExecutionModel::FineGrained,
                16
            },
            {
                heat2d::benchmark::ExecutionModel::FineGrained,
                32
            },
            {
                heat2d::benchmark::ExecutionModel::FineGrained,
                64
            },

            {
                heat2d::benchmark::ExecutionModel::CoarseGrained,
                1
            },
            {
                heat2d::benchmark::ExecutionModel::CoarseGrained,
                2
            },
            {
                heat2d::benchmark::ExecutionModel::CoarseGrained,
                4
            },
            {
                heat2d::benchmark::ExecutionModel::CoarseGrained,
                8
            },
            {
                heat2d::benchmark::ExecutionModel::CoarseGrained,
                16
            },
            {
                heat2d::benchmark::ExecutionModel::CoarseGrained,
                32
            },
            {
                heat2d::benchmark::ExecutionModel::CoarseGrained,
                64
            }
        };

        const auto coarseBlockingMicroseconds =
            std::chrono::duration_cast<
                std::chrono::microseconds
            >(
                config.coarseBlockingDuration
            ).count();

        const std::size_t interiorRows =
            config.rows - 2;

        const std::size_t eventsPerStep =
            interiorRows /
                config.coarseRowsPerBlockingEvent
            +
            (
                interiorRows %
                    config.coarseRowsPerBlockingEvent != 0
                    ? 1
                    : 0
            );

        const std::size_t coarseEventsPerRun =
            eventsPerStep *
            config.steps;

        std::cout
            << "\n=== Heat2D Common Benchmark ===\n\n";

        std::cout
            << "Grid: "
            << config.rows
            << " x "
            << config.cols
            << '\n';

        std::cout
            << "Steps: "
            << config.steps
            << '\n';

        std::cout
            << "dt: "
            << config.dt
            << " s\n";

        std::cout
            << "Cell updates per run: "
            << heat2d::benchmark::calculateCellUpdates(
                config
            )
            << '\n';

        std::cout
            << "Cases: "
            << cases.size()
            << '\n';

        std::cout
            << "Warm-up rounds: "
            << config.warmupRuns
            << '\n';

        std::cout
            << "Measured rounds: "
            << config.measuredRuns
            << '\n';

        std::cout
            << "\n--- Coarse-Grained Policy ---\n";

        std::cout
            << "Blocking duration: "
            << coarseBlockingMicroseconds
            << " us\n";

        std::cout
            << "Rows per blocking event: "
            << config.coarseRowsPerBlockingEvent
            << '\n';

        std::cout
            << "Blocking events per timestep: "
            << eventsPerStep
            << '\n';

        std::cout
            << "Blocking events per CG run: "
            << coarseEventsPerRun
            << '\n';

        std::cout
            << "\nValidating numerical equivalence...\n";

        heat2d::benchmark::validateBenchmarkCases(
            config,
            cases
        );

        std::cout
            << "Validation: PASSED\n";

        std::cout
            << "\nRunning warm-ups...\n";

        heat2d::benchmark::runWarmups(
            config,
            cases
        );

        std::cout
            << "Warm-ups completed.\n";

        std::cout
            << "\nRunning measured executions...\n";

        const auto samples =
            heat2d::benchmark::runBenchmark(
                config,
                cases
            );

        writeCsv(
            outputPath,
            samples
        );

        std::cout
            << "Measured executions completed: "
            << samples.size()
            << '\n';

        std::cout
            << "Raw results written to: "
            << outputPath
            << '\n';

        std::cout
            << "\nBenchmark completed successfully.\n";

        return 0;

    } catch (const std::exception& error) {
        std::cerr
            << "\nBenchmark failed.\n"
            << "Reason: "
            << error.what()
            << '\n';

        return 1;
    }
}
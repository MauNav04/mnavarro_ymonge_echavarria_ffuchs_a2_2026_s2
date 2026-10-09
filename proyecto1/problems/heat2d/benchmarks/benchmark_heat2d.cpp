#include "benchmark/benchmark.hpp"

#include <chrono>
#include <cstddef>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

/**
 * @brief Parses a strictly positive integer command-line argument.
 */
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

    if (
        parsed >
        static_cast<unsigned long long>(
            std::numeric_limits<std::size_t>::max()
        )
    ) {
        throw std::out_of_range(
            std::string(name) +
            " exceeds std::size_t capacity."
        );
    }

    return static_cast<std::size_t>(
        parsed
    );
}


/**
 * @brief Validates and returns the selected benchmark campaign.
 *
 * Supported values:
 *   all -> Sequential + Fine-Grained + Coarse-Grained
 *   fg  -> Sequential + Fine-Grained
 *   cg  -> Sequential + Coarse-Grained
 */
std::string parseCampaignModel(
    int argc,
    char* argv[]
) {
    const std::string campaignModel =
        argc >= 5
            ? argv[4]
            : "all";

    if (
        campaignModel != "all" &&
        campaignModel != "fg" &&
        campaignModel != "cg"
    ) {
        throw std::invalid_argument(
            "Benchmark model must be one of: all, fg, cg."
        );
    }

    return campaignModel;
}


/**
 * @brief Builds the execution cases belonging to one campaign.
 *
 * Sequential is always included because it is the common reference
 * used to calculate paired speedup.
 */
std::vector<
    heat2d::benchmark::BenchmarkCase
> createBenchmarkCases(
    const std::string& campaignModel
) {
    using heat2d::benchmark::BenchmarkCase;
    using heat2d::benchmark::ExecutionModel;

    std::vector<BenchmarkCase> cases;

    /*
     * Sequential reference.
     */
    cases.push_back({
        ExecutionModel::Sequential,
        1
    });

    /*
     * Common execution-context counts used by both models.
     */
    const std::vector<std::size_t> threadCounts{
        1,
        2,
        4,
        8,
        16,
        32,
        64
    };

    /*
     * Fine-Grained campaign.
     *
     * These values represent simulated logical contexts.
     */
    if (
        campaignModel == "all" ||
        campaignModel == "fg"
    ) {
        for (
            const std::size_t threadCount :
            threadCounts
        ) {
            cases.push_back({
                ExecutionModel::FineGrained,
                threadCount
            });
        }
    }

    /*
     * Coarse-Grained campaign.
     *
     * These values represent real std::thread workers.
     */
    if (
        campaignModel == "all" ||
        campaignModel == "cg"
    ) {
        for (
            const std::size_t threadCount :
            threadCounts
        ) {
            cases.push_back({
                ExecutionModel::CoarseGrained,
                threadCount
            });
        }
    }

    return cases;
}


/**
 * @brief Writes every measured benchmark observation to CSV.
 */
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
     * logical_threads is preserved for compatibility with the
     * statistical analysis scripts.
     *
     * Meaning by model:
     *
     * Sequential:
     *   always 1.
     *
     * Fine-Grained:
     *   simulated logical contexts.
     *
     * Coarse-Grained:
     *   real worker threads.
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
        /*
         * Command-line format:
         *
         * ./heat2d_benchmark
         *      [measured_runs]
         *      [warmup_runs]
         *      [output.csv]
         *      [all|fg|cg]
         */
        if (argc > 5) {
            throw std::invalid_argument(
                "Usage: ./heat2d_benchmark "
                "[measured_runs] "
                "[warmup_runs] "
                "[output.csv] "
                "[all|fg|cg]"
            );
        }

        heat2d::benchmark::BenchmarkConfig config;

        /*
         * Optional number of measured rounds.
         */
        if (argc >= 2) {
            config.measuredRuns =
                parsePositiveInteger(
                    argv[1],
                    "Measured runs"
                );
        }

        /*
         * Optional number of discarded warm-up rounds.
         */
        if (argc >= 3) {
            config.warmupRuns =
                parsePositiveInteger(
                    argv[2],
                    "Warm-up runs"
                );
        }

        /*
         * Optional raw CSV destination.
         */
        const std::string outputPath =
            argc >= 4
                ? argv[3]
                : "heat2d_benchmark.csv";

        /*
         * Optional campaign selector.
         *
         * Default:
         *   all
         */
        const std::string campaignModel =
            parseCampaignModel(
                argc,
                argv
            );

        /*
         * Build only the cases belonging to the selected campaign.
         *
         * fg:
         *   Sequential + FG {1,2,4,8,16,32,64}
         *   = 8 cases.
         *
         * cg:
         *   Sequential + CG {1,2,4,8,16,32,64}
         *   = 8 cases.
         *
         * all:
         *   Sequential + 7 FG + 7 CG
         *   = 15 cases.
         */
        const std::vector<
            heat2d::benchmark::BenchmarkCase
        > cases =
            createBenchmarkCases(
                campaignModel
            );

        /*
         * Coarse-Grained policy metadata.
         *
         * These calculations are useful when CG participates in
         * the selected campaign.
         */
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

        /*
         * Benchmark information.
         */
        std::cout
            << "\n=== Heat2D Common Benchmark ===\n\n";

        std::cout
            << "Campaign model: "
            << campaignModel
            << '\n';

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

        /*
         * Fine-Grained policy information.
         */
        if (
            campaignModel == "fg" ||
            campaignModel == "all"
        ) {
            std::cout
                << "\n--- Fine-Grained Policy ---\n";

            std::cout
                << "Logical contexts: "
                << "1, 2, 4, 8, 16, 32, 64\n";

            std::cout
                << "Scheduler: Round-Robin\n";

            std::cout
                << "Quantum: 1 cell update\n";

            std::cout
                << "Execution: simulated logical contexts\n";
        }

        /*
         * Coarse-Grained policy information.
         */
        if (
            campaignModel == "cg" ||
            campaignModel == "all"
        ) {
            std::cout
                << "\n--- Coarse-Grained Policy ---\n";

            std::cout
                << "Worker threads: "
                << "1, 2, 4, 8, 16, 32, 64\n";

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
        }

        /*
         * Validate every selected case against the sequential
         * numerical reference before timing begins.
         */
        std::cout
            << "\nValidating numerical equivalence...\n";

        heat2d::benchmark::validateBenchmarkCases(
            config,
            cases
        );

        std::cout
            << "Validation: PASSED\n";

        /*
         * Execute discarded warm-up rounds.
         */
        std::cout
            << "\nRunning warm-ups...\n";

        heat2d::benchmark::runWarmups(
            config,
            cases
        );

        std::cout
            << "Warm-ups completed.\n";

        /*
         * Execute measured rounds.
         */
        std::cout
            << "\nRunning measured executions...\n";

        const auto samples =
            heat2d::benchmark::runBenchmark(
                config,
                cases
            );

        /*
         * Raw CSV writing is outside the timed regions.
         */
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
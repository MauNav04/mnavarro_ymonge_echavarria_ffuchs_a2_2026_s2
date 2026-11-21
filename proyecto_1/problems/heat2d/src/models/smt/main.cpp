#include "smt/smt.hpp"

#include "config.hpp"
#include "grid.hpp"
#include "heat2d.hpp"
#include "initial_conditions.hpp"
#include "smt/cpu_topology.hpp"

#include <chrono>
#include <cstddef>
#include <exception>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>


namespace {

/**
 * @brief Parses a positive worker count from the command line.
 * 
 * Extracts the requested number of Simultaneous Multithreading (SMT) workers. 
 * Fast-fails on missing, non-numeric, or zero input to prevent undefined 
 * behavior during thread pool allocation.
 *
 * @throws std::invalid_argument If the string cannot be parsed into a positive integer.
 */
std::size_t parseThreadCount(
    const char* text
) {
    const std::string value(
        text
    );

    std::size_t processedCharacters =
        0;

    unsigned long long parsedValue =
        0;

    try {
        parsedValue =
            std::stoull(
                value,
                &processedCharacters
            );

    } catch (
        const std::exception&
    ) {
        throw std::invalid_argument(
            "SMT worker count must be a positive integer."
        );
    }

    if (
        processedCharacters !=
        value.size()
    ) {
        throw std::invalid_argument(
            "SMT worker count contains invalid characters."
        );
    }

    if (
        parsedValue == 0
    ) {
        throw std::invalid_argument(
            "SMT worker count must be greater than zero."
        );
    }

    return static_cast<std::size_t>(
        parsedValue
    );
}


/**
 * @brief Prints the Linux CPU topology detected for the current process.
 * 
 * Outputs the exact hardware configuration detected through `sysfs`, differentiating 
 * between true physical cores and available logical CPUs (threads). This clarifies 
 * the baseline for measuring hardware SMT acceleration vs software time-slicing.
 */
void printTopology(
    const heat2d::smt::CpuTopology& topology
) {
    using namespace heat2d::smt;

    std::cout
        << "\n--- Linux CPU Topology ---\n";

    std::cout
        << "Linux SMT control: "
        << topology.linuxSmtControl
        << '\n';

    std::cout
        << "Physical cores: "
        << physicalCoreCount(
            topology
        )
        << '\n';

    std::cout
        << "Logical CPUs: "
        << logicalCpuCount(
            topology
        )
        << '\n';

    std::cout
        << "Min threads/core: "
        << minimumThreadsPerCore(
            topology
        )
        << '\n';

    std::cout
        << "Max threads/core: "
        << maximumThreadsPerCore(
            topology
        )
        << '\n';

    std::cout
        << "SMT available: "
        << (
            smtAvailable(
                topology
            )
                ? "YES"
                : "NO"
        )
        << '\n';
}


/**
 * @brief Prints the exact logical CPU placement used by the workers.
 * 
 * Maps every worker ID to its pinned logical CPU. This is crucial for verifying 
 * that the `pthread_setaffinity_np` logic successfully saturated distinct physical 
 * cores before placing secondary threads on the same physical core (HyperThreading).
 */
void printCpuAssignments(
    const heat2d::smt::SmtStats& stats
) {
    std::cout
        << "Worker placement: ";

    for (
        std::size_t workerId = 0;
        workerId <
            stats.logicalCpuIds.size();
        ++workerId
    ) {
        if (
            workerId != 0
        ) {
            std::cout
                << ' ';
        }

        std::cout
            << "[W"
            << workerId
            << "->CPU"
            << stats.logicalCpuIds[
                workerId
            ]
            << "]";
    }

    std::cout
        << '\n';
}

}  // namespace

/**
 * @brief Standalone executable entry point for the Simultaneous Multithreading (SMT) model.
 * 
 * 1. Probes Linux `sysfs` to determine the physical topology and available logical CPUs.
 * 2. Defaults to occupying every available logical CPU unless overridden by the user.
 * 3. Identifies if the execution will leverage true hardware SMT or software oversubscription.
 * 4. Pings threads to specific logical CPUs using the optimal breadth-first assignment.
 * 5. Executes the compute kernel and measures execution time and throughput.
 */
int main(
    int argc,
    char* argv[]
) {
    try {
        if (
            argc > 2
        ) {
            throw std::invalid_argument(
                "Usage: heat2d_smt [worker_threads]"
            );
        }

        /*
         * Detect CPU topology before choosing the default worker count.
         */
        const heat2d::smt::CpuTopology topology =
            heat2d::smt::detectCpuTopology();

        const std::size_t availableLogicalCpus =
            heat2d::smt::logicalCpuCount(
                topology
            );

        /*
         * By default use every logical CPU visible to the process.
         *
         * On the target Ryzen 8C/16T machine this should select
         * 16 worker threads when SMT is enabled.
         */
        const std::size_t requestedThreads =
            argc == 2
                ? parseThreadCount(
                    argv[1]
                )
                : availableLogicalCpus;

        const std::size_t physicalCores =
            heat2d::smt::physicalCoreCount(
                topology
            );

        const std::size_t maxSupported =
            std::max(
                availableLogicalCpus,
                physicalCores * 4
            );

        // Sanity check to prevent out-of-memory or extreme OS thrashing.
        if (
            requestedThreads >
            maxSupported
        ) {
            throw std::invalid_argument(
                "Requested SMT worker count exceeds maximum supported threads (" +
                std::to_string(maxSupported) + ")."
            );
        }

        const heat2d::SimulationConfig config;

        std::cout
            << "\n========================================\n"
            << " Heat2D SMT Simulation\n"
            << "========================================\n";

        printTopology(
            topology
        );

        // Distinguish between true Hardware SMT and OS-level Software oversubscription.
        const bool isHardwareSmt =
            heat2d::smt::smtAvailable(topology) && requestedThreads <= availableLogicalCpus;

        std::cout
            << "Mode: "
            << (isHardwareSmt
                    ? "Hardware SMT Real (SMT ON)"
                    : "Software Oversubscription (SMT OFF)")
            << '\n';

        std::cout
            << "\n--- Simulation Configuration ---\n";

        std::cout
            << std::fixed
            << std::setprecision(6);

        std::cout
            << "Surface size: "
            << config.lengthX
            << " m x "
            << config.lengthY
            << " m\n";

        std::cout
            << "Grid size: "
            << config.rows
            << " x "
            << config.cols
            << '\n';

        std::cout
            << "dx: "
            << config.dx()
            << " m\n";

        std::cout
            << "dy: "
            << config.dy()
            << " m\n";

        std::cout
            << "Alpha: "
            << config.alpha
            << '\n';

        std::cout
            << "dt: "
            << config.dt
            << " s\n";

        std::cout
            << "Requested simulation time: "
            << config.totalTime
            << " s\n";

        std::cout
            << "Time steps: "
            << config.steps()
            << '\n';

        std::cout
            << "SMT worker threads: "
            << requestedThreads
            << '\n';

        // Pre-flight check: Forward Euler stability criterion.
        const bool stable =
            heat2d::isStable(
                config.alpha,
                config.dt,
                config.dx(),
                config.dy()
            );

        std::cout
            << "Numerical stability: "
            << (
                stable
                    ? "STABLE"
                    : "UNSTABLE"
            )
            << '\n';

        if (
            !stable
        ) {
            throw std::runtime_error(
                "Default Heat2D configuration is numerically unstable."
            );
        }

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

        const std::size_t centerRow =
            config.rows / 2;

        const std::size_t centerColumn =
            config.cols / 2;

        const double initialCenterTemperature =
            current(
                centerRow,
                centerColumn
            );

        heat2d::smt::SmtConfig smtConfig;

        smtConfig.threadCount =
            requestedThreads;

        // Allow creating more threads than logical CPUs, forcing OS time-slicing.
        smtConfig.allowOversubscription =
            true;

        // === TIMING REGION START ===
        const auto start =
            std::chrono::steady_clock::now();

        const heat2d::smt::SmtStats stats =
            heat2d::smt::runSmt(
                current,
                next,
                config.steps(),
                config.alpha,
                config.dt,
                config.dx(),
                config.dy(),
                config.boundaryTemperature,
                smtConfig
            );

        const auto end =
            std::chrono::steady_clock::now();
        // === TIMING REGION END ===

        const double elapsedSeconds =
            std::chrono::duration<double>(
                end - start
            ).count();

        const double finalCenterTemperature =
            current(
                centerRow,
                centerColumn
            );

        const double throughput =
            elapsedSeconds > 0.0
                ? static_cast<double>(
                    stats.executedCellUpdates
                ) /
                    elapsedSeconds
                : 0.0;

        std::cout
            << "\n--- Results ---\n";

        std::cout
            << "Initial center temperature: "
            << initialCenterTemperature
            << " C\n";

        std::cout
            << "Final center temperature: "
            << finalCenterTemperature
            << " C\n";

        std::cout
            << "\n--- SMT Execution Statistics ---\n";

        std::cout
            << "Worker threads: "
            << stats.threadCount
            << '\n';

        std::cout
            << "Pinned workers: "
            << stats.pinnedWorkerCount
            << '\n';

        std::cout
            << "Available physical cores: "
            << stats.availablePhysicalCores
            << '\n';

        std::cout
            << "Available logical CPUs: "
            << stats.availableLogicalCpus
            << '\n';

        std::cout
            << "Time steps: "
            << stats.timeSteps
            << '\n';

        std::cout
            << "Cell updates: "
            << stats.executedCellUpdates
            << '\n';

        std::cout
            << "Barrier phases: "
            << stats.barrierPhases
            << '\n';

        printCpuAssignments(
            stats
        );

        std::cout
            << std::setprecision(9);

        std::cout
            << "Execution time: "
            << elapsedSeconds
            << " s\n";

        std::cout
            << "Throughput: "
            << throughput
            << " cell updates/s\n";

        std::cout
            << "\nSMT simulation completed successfully.\n\n";

        return 0;

    } catch (
        const std::exception& error
    ) {
        std::cerr
            << "\nSMT simulation failed.\n"
            << "Reason: "
            << error.what()
            << '\n';

        return 1;
    }
}
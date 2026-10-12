#pragma once

#include "grid.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace heat2d::smt {

/**
 * @brief Configuration for the topology-aware SMT execution model.
 *
 * Every worker is pinned to exactly one logical CPU selected from the
 * topology-aware SMT ordering.
 *
 * Oversubscription is intentionally rejected. Therefore threadCount
 * cannot exceed the number of logical CPUs available to the process.
 */
struct SmtConfig {
    std::size_t threadCount{1};
};


/**
 * @brief Execution metadata returned by the SMT solver.
 *
 * logicalCpuIds records the exact Linux logical CPUs assigned to the
 * worker threads. This information is useful for reproducibility and
 * for proving that the SMT experiment used the intended topology.
 */
struct SmtStats {
    std::size_t threadCount{0};

    std::size_t timeSteps{0};

    std::uint64_t executedCellUpdates{0};

    std::uint64_t barrierPhases{0};

    std::size_t pinnedWorkerCount{0};

    std::size_t availablePhysicalCores{0};

    std::size_t availableLogicalCpus{0};

    std::vector<int> logicalCpuIds;
};


/**
 * @brief Executes Heat2D using topology-aware SMT worker placement.
 *
 * Execution model:
 *
 *   - persistent std::thread workers,
 *   - static contiguous row partition,
 *   - one worker pinned to one Linux logical CPU,
 *   - topology-aware SMT ordering,
 *   - shared read-only current grid,
 *   - disjoint writes into next,
 *   - two synchronization barriers per timestep,
 *   - fixed Dirichlet boundary conditions,
 *   - double buffering via current/next swap.
 *
 * The CPU placement order is obtained from buildSmtCpuOrder().
 *
 * Therefore workers consume one logical CPU from every physical core
 * before using additional SMT siblings.
 *
 * @throws std::invalid_argument for invalid numerical or execution
 *         configuration.
 * @throws std::runtime_error when CPU topology or thread affinity
 *         cannot be established safely.
 */
[[nodiscard]]
SmtStats runSmt(
    Grid& current,
    Grid& next,
    std::size_t steps,
    double alpha,
    double dt,
    double dx,
    double dy,
    double boundaryTemperature,
    const SmtConfig& config
);

}  // namespace heat2d::smt
#pragma once

#include "grid.hpp"

#include <cstddef>
#include <cstdint>

namespace heat2d {

/**
 * @brief Configuration parameters for the CMP execution model.
 *
 * CMP uses real operating-system threads that may execute
 * simultaneously on different CPU cores.
 */
struct CmpConfig {

    /**
     * @brief Number of worker threads used by the simulation.
     *
     * Each worker receives a contiguous range of interior rows.
     */
    std::size_t threadCount{1};
};


/**
 * @brief Execution statistics produced by the CMP model.
 */
struct CmpStats {

    /**
     * @brief Number of real worker threads created.
     */
    std::size_t threadCount{0};

    /**
     * @brief Number of simulated time steps.
     */
    std::size_t timeSteps{0};

    /**
     * @brief Total number of interior-cell updates performed.
     */
    std::uint64_t executedCellUpdates{0};

    /**
     * @brief Number of global synchronization phases.
     *
     * CMP performs two barrier phases per timestep:
     *
     * 1. Wait until every worker has finished computing next.
     * 2. Wait until the boundary update and buffer swap are complete.
     */
    std::uint64_t barrierPhases{0};
};


/**
 * @brief Executes the Heat2D simulation using real multicore
 *        parallelism through std::thread workers.
 *
 * The interior rows are statically partitioned among the requested
 * number of workers. During each timestep every worker reads from
 * current and writes only to its assigned rows in next.
 *
 * Workers synchronize twice per timestep:
 *
 *   compute next
 *       |
 *       v
 *   barrier #1
 *       |
 *       v
 *   thread 0 applies boundaries and swaps current/next
 *       |
 *       v
 *   barrier #2
 *
 * @param current Grid containing the current simulation state.
 * @param next Auxiliary grid used to store the next state.
 * @param steps Number of simulation timesteps.
 * @param alpha Thermal diffusivity.
 * @param dt Temporal step.
 * @param dx Horizontal spatial spacing.
 * @param dy Vertical spatial spacing.
 * @param boundaryTemperature Fixed Dirichlet boundary temperature.
 * @param config CMP execution configuration.
 *
 * @return Statistics describing the CMP execution.
 */
CmpStats runCmp(
    Grid& current,
    Grid& next,
    std::size_t steps,
    double alpha,
    double dt,
    double dx,
    double dy,
    double boundaryTemperature,
    const CmpConfig& config
);

}  // namespace heat2d
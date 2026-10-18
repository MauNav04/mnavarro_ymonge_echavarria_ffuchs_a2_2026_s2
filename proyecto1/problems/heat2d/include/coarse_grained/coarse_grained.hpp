#pragma once

#include "grid.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>

namespace heat2d {

/**
 * @brief Configuration specific to the Coarse-Grained model.
 *
 * The model uses real threads. Each worker processes contiguous
 * row regions and, after a configurable number of rows,
 * may experience a synthetic high-latency blocking event.
 */
struct CoarseGrainedConfig {
    /**
     * Number of real workers used by the simulation.
     */
    std::size_t threadCount{1};

    /**
     * Requested duration for each synthetic blocking event.
     *
     * A duration of zero disables blocking.
     */
    std::chrono::nanoseconds blockingDuration{0};

    /**
     * Number of interior rows processed between synthetic
     * blocking events.
     *
     * The formal value proposed for the experiments is 8.
     */
    std::size_t rowsPerBlockingEvent{8};
};

/**
 * @brief Internal statistics of a Coarse-Grained execution.
 */
struct CoarseGrainedStats {
    std::size_t threadCount{0};
    std::size_t timeSteps{0};

    /**
     * Total number of interior cell updates.
     */
    std::uint64_t executedCellUpdates{0};

    /**
     * Total number of synthetic blocking events executed.
     *
     * If blockingDuration == 0, the value is zero.
     */
    std::uint64_t blockingEvents{0};

    /**
     * Number of global barrier phases.
     *
     * Two are used per timestep:
     *  1. Completion of the next computation.
     *  2. Completion of boundary + swap.
     */
    std::uint64_t barrierPhases{0};
};

/**
 * @brief Runs Heat2D using the Coarse-Grained model.
 *
 * The workers are real std::threads. Each one processes a contiguous
 * range of interior rows. Synthetic blocking events occur after large
 * blocks of rows, while a reusable barrier maintains consistency
 * between timesteps.
 *
 * @param current Grid corresponding to the current state.
 * @param next Auxiliary grid for the next timestep.
 * @param steps Number of timesteps.
 * @param alpha Thermal diffusivity.
 * @param dt Time step.
 * @param dx Horizontal spatial separation.
 * @param dy Vertical spatial separation.
 * @param boundaryTemperature Fixed boundary temperature.
 * @param coarseConfig Configuration specific to Coarse-Grained.
 *
 * @return Statistics of the execution.
 */
[[nodiscard]]
CoarseGrainedStats runCoarseGrained(
    Grid& current,
    Grid& next,
    std::size_t steps,
    double alpha,
    double dt,
    double dx,
    double dy,
    double boundaryTemperature,
    const CoarseGrainedConfig& coarseConfig
);

} // namespace heat2d
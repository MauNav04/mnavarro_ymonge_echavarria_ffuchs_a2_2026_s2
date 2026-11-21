#pragma once

#include "grid.hpp"

#include <cstddef>

namespace heat2d::fine_grained {

/**
 * @brief Basic metrics of a Fine-Grained execution.
 *
 * They allow validating that the model has consumed exactly
 * the expected quantums during the simulation.
 */
struct FineGrainedStats {
    std::size_t logicalThreadCount;
    std::size_t timeSteps;
    std::size_t executedQuanta;
};

/**
 * @brief Runs the Heat2D simulation using Fine-Grained Multithreading.
 *
 * The interior domain is divided among logical threads. Each context
 * executes exactly one cell per quantum and the scheduler switches
 * threads at each decision using Round-Robin.
 *
 * @param current Grid corresponding to the current state.
 * @param next Grid used as a buffer for the next state.
 * @param steps Number of time steps.
 * @param logicalThreadCount Number of simulated logical threads.
 * @param alpha Thermal diffusivity.
 * @param dt Time step.
 * @param dx Horizontal spatial separation.
 * @param dy Vertical spatial separation.
 * @param boundaryTemperature Fixed temperature of the boundaries.
 *
 * @return Statistics of the Fine-Grained execution.
 *
 * @throws std::invalid_argument if the configuration is invalid.
 */
[[nodiscard]]
FineGrainedStats runFineGrained(
    Grid& current,
    Grid& next,
    std::size_t steps,
    std::size_t logicalThreadCount,
    double alpha,
    double dt,
    double dx,
    double dy,
    double boundaryTemperature
);

}  // namespace heat2d::fine_grained
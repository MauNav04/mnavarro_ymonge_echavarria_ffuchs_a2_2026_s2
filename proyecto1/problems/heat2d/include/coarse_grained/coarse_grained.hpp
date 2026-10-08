#pragma once

#include "grid.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>

namespace heat2d {

/**
 * @brief Parámetros específicos del modelo Coarse-Grained.
 */
struct CoarseGrainedConfig {
    std::size_t threadCount{1};

    /**
     * Tiempo de espera sintético después de procesar una fila.
     *
     * Representa un evento bloqueante de alta latencia.
     * No debe interpretarse como la latencia real de un cache miss.
     */
    std::chrono::nanoseconds blockingDuration{0};
};

/**
 * @brief Métricas internas del modelo Coarse-Grained.
 */
struct CoarseGrainedStats {
    std::size_t threadCount{0};
    std::size_t timeSteps{0};

    std::uint64_t executedCellUpdates{0};
    std::uint64_t blockingEvents{0};
    std::uint64_t barrierPhases{0};
};

/**
 * @brief Ejecuta Heat2D con multithreading de grano grueso.
 *
 * Los workers son std::thread reales que trabajan concurrentemente.
 * Cada worker procesa bloques completos de filas y puede experimentar
 * una espera bloqueante sintética después de cada fila.
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
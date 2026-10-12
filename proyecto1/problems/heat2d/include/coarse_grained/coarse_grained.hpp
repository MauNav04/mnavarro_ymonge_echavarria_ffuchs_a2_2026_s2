#pragma once

#include "grid.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>

namespace heat2d {

/**
 * @brief Configuración específica del modelo Coarse-Grained.
 *
 * El modelo utiliza threads reales. Cada worker procesa regiones
 * contiguas de filas y, después de una cantidad configurable de filas,
 * puede experimentar un evento bloqueante sintético de alta latencia.
 */
struct CoarseGrainedConfig {
    /**
     * Cantidad de workers reales utilizados por la simulación.
     */
    std::size_t threadCount{1};

    /**
     * Duración solicitada para cada evento bloqueante sintético.
     *
     * Una duración de cero desactiva los bloqueos.
     */
    std::chrono::nanoseconds blockingDuration{0};

    /**
     * Cantidad de filas interiores procesadas entre eventos
     * bloqueantes sintéticos.
     *
     * El valor formal propuesto para los experimentos es 8.
     */
    std::size_t rowsPerBlockingEvent{8};
};

/**
 * @brief Estadísticas internas de una ejecución Coarse-Grained.
 */
struct CoarseGrainedStats {
    std::size_t threadCount{0};
    std::size_t timeSteps{0};

    /**
     * Cantidad total de actualizaciones de celdas interiores.
     */
    std::uint64_t executedCellUpdates{0};

    /**
     * Cantidad total de eventos bloqueantes sintéticos ejecutados.
     *
     * Si blockingDuration == 0, el valor es cero.
     */
    std::uint64_t blockingEvents{0};

    /**
     * Cantidad de fases globales de barrera.
     *
     * Se utilizan dos por timestep:
     *  1. Finalización del cálculo de next.
     *  2. Finalización de boundary + swap.
     */
    std::uint64_t barrierPhases{0};
};

/**
 * @brief Ejecuta Heat2D utilizando el modelo Coarse-Grained.
 *
 * Los workers son std::thread reales. Cada uno procesa un rango
 * contiguo de filas interiores. Los eventos bloqueantes sintéticos
 * ocurren después de bloques grandes de filas, mientras que una
 * barrera reutilizable mantiene la consistencia entre timesteps.
 *
 * @param current Malla correspondiente al estado actual.
 * @param next Malla auxiliar para el siguiente timestep.
 * @param steps Cantidad de timesteps.
 * @param alpha Difusividad térmica.
 * @param dt Paso temporal.
 * @param dx Separación espacial horizontal.
 * @param dy Separación espacial vertical.
 * @param boundaryTemperature Temperatura fija de frontera.
 * @param coarseConfig Configuración específica de Coarse-Grained.
 *
 * @return Estadísticas de la ejecución.
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
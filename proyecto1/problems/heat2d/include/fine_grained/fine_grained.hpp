#pragma once

#include "grid.hpp"

#include <cstddef>

namespace heat2d::fine_grained {

/**
 * @brief Métricas básicas de una ejecución Fine-Grained.
 *
 * Permiten validar que el modelo haya consumido exactamente
 * los quantums esperados durante la simulación.
 */
struct FineGrainedStats {
    std::size_t logicalThreadCount;
    std::size_t timeSteps;
    std::size_t executedQuanta;
};

/**
 * @brief Ejecuta la simulación Heat2D utilizando Fine-Grained Multithreading.
 *
 * El dominio interior se divide entre hilos lógicos. Cada contexto
 * ejecuta exactamente una celda por quantum y el scheduler cambia
 * de hilo en cada decisión utilizando Round-Robin.
 *
 * @param current Malla correspondiente al estado actual.
 * @param next Malla utilizada como buffer del siguiente estado.
 * @param steps Cantidad de pasos temporales.
 * @param logicalThreadCount Cantidad de hilos lógicos simulados.
 * @param alpha Difusividad térmica.
 * @param dt Paso temporal.
 * @param dx Separación espacial horizontal.
 * @param dy Separación espacial vertical.
 * @param boundaryTemperature Temperatura fija de los bordes.
 *
 * @return Estadísticas de la ejecución Fine-Grained.
 *
 * @throws std::invalid_argument si la configuración es inválida.
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
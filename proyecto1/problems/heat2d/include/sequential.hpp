#pragma once  // Evita que este archivo de cabecera sea incluido múltiples veces durante la compilación.

#include "grid.hpp"  // Incluye la definición de Grid utilizada por la simulación.

#include <cstddef>  // Proporciona std::size_t para representar la cantidad de pasos temporales.

namespace heat2d {  // Agrupa la implementación secuencial dentro del namespace del proyecto.

/**
 * @brief Ejecuta la simulación secuencial de difusión de calor 2D.
 *
 * La función avanza la simulación durante una cantidad determinada
 * de pasos temporales. En cada iteración se calcula el nuevo estado
 * utilizando diffusionStep() y posteriormente se intercambian las
 * mallas current y next.
 *
 * Al finalizar la función, current contiene el estado final de la
 * superficie después de ejecutar todos los pasos temporales.
 *
 * @param current Malla que contiene inicialmente el estado de la superficie.
 * @param next Malla auxiliar utilizada para almacenar el siguiente estado.
 * @param steps Cantidad de pasos temporales que se deben ejecutar.
 * @param alpha Difusividad térmica del material.
 * @param dt Tamaño de cada paso temporal.
 * @param dx Separación espacial entre columnas.
 * @param dy Separación espacial entre filas.
 * @param boundaryTemperature Temperatura fija aplicada a los cuatro bordes.
 */
void runSequential(
    Grid& current,
    Grid& next,
    std::size_t steps,
    double alpha,
    double dt,
    double dx,
    double dy,
    double boundaryTemperature
);

}  // namespace heat2d
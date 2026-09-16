#pragma once  // Evita que este archivo de cabecera sea incluido más de una vez durante la compilación.

#include "grid.hpp"  // Incluye la clase Grid utilizada para representar las temperaturas de la superficie.

namespace heat2d {  // Agrupa las funciones relacionadas con la simulación Heat Diffusion 2D.

/**
 * @brief Comprueba si los parámetros numéricos cumplen la condición de estabilidad.
 *
 * El método explícito de diferencias finitas utilizado para resolver la
 * ecuación del calor en dos dimensiones requiere cumplir:
 *
 * alpha * dt * (1 / dx^2 + 1 / dy^2) <= 1 / 2
 *
 * Si dx == dy == h, esta expresión se reduce a:
 *
 * alpha * dt / h^2 <= 1 / 4
 *
 * @param alpha Difusividad térmica del material.
 * @param dt Tamaño del paso temporal.
 * @param dx Separación espacial entre columnas.
 * @param dy Separación espacial entre filas.
 * @return true si la configuración es numéricamente estable.
 * @return false si la configuración viola la condición de estabilidad.
 */
bool isStable(
    double alpha,
    double dt,
    double dx,
    double dy
);

/**
 * @brief Calcula un único paso temporal de difusión de calor bidimensional.
 *
 * La función utiliza las temperaturas almacenadas en current para calcular
 * las temperaturas correspondientes al instante siguiente y las almacena
 * dentro de next.
 *
 * current nunca se modifica durante el cálculo. Esto evita utilizar valores
 * parcialmente actualizados dentro de una misma iteración temporal.
 *
 * @param current Malla correspondiente al tiempo actual.
 * @param next Malla donde se almacenará el siguiente estado.
 * @param alpha Difusividad térmica del material.
 * @param dt Tamaño del paso temporal.
 * @param dx Separación espacial entre columnas.
 * @param dy Separación espacial entre filas.
 * @param boundaryTemperature Temperatura fija utilizada en los cuatro bordes.
 */
void diffusionStep(
    const Grid& current,
    Grid& next,
    double alpha,
    double dt,
    double dx,
    double dy,
    double boundaryTemperature
);

}  // namespace heat2d
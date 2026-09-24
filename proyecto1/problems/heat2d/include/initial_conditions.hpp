#pragma once  // Evita que este archivo de cabecera sea incluido más de una vez durante la compilación.

#include "grid.hpp"  // Incluye la definición de Grid para poder modificar la superficie de temperaturas.

namespace heat2d {  // Agrupa estas funciones dentro del espacio de nombres del proyecto Heat2D.

/**
 * @brief Inicializa la superficie utilizando una distribución sinusoidal bidimensional.
 *
 * La temperatura inicial se calcula mediante:
 *
 * T(x,y) = Tmax * sin(pi*x/Lx) * sin(pi*y/Ly)
 *
 * Esta distribución produce temperatura cero en los bordes y una
 * temperatura máxima aproximadamente en el centro de la superficie.
 *
 * @param grid Malla bidimensional donde se almacenarán las temperaturas.
 * @param maxTemperature Temperatura máxima de la distribución inicial.
 * @param lengthX Longitud física de la superficie en el eje X.
 * @param lengthY Longitud física de la superficie en el eje Y.
 */
void initializeSinusoidal(
    Grid& grid,
    double maxTemperature,
    double lengthX,
    double lengthY
);

/**
 * @brief Establece una temperatura fija en los cuatro bordes de la superficie.
 *
 * Esta función implementa una condición de frontera de Dirichlet,
 * donde la temperatura de los bordes permanece fijada a un valor conocido.
 *
 * @param grid Malla bidimensional que será modificada.
 * @param boundaryTemperature Temperatura asignada a los cuatro bordes.
 */
void applyFixedBoundary(
    Grid& grid,
    double boundaryTemperature
);

}  // namespace heat2d
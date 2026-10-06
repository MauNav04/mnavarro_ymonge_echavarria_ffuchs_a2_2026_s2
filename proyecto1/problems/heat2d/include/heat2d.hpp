#pragma once

#include "grid.hpp"

#include <cstddef>

namespace heat2d {

/**
 * @brief Coeficientes constantes del esquema explícito de difusión.
 */
struct DiffusionCoefficients {
    double x;
    double y;
};

/**
 * @brief Comprueba la estabilidad numérica del esquema explícito 2D.
 *
 * Se requiere:
 *
 * alpha * dt * (1 / dx^2 + 1 / dy^2) <= 1 / 2
 */
[[nodiscard]]
bool isStable(
    double alpha,
    double dt,
    double dx,
    double dy
);

/**
 * @brief Valida los parámetros físicos y genera los coeficientes
 *        utilizados durante un paso de difusión.
 *
 * @throws std::invalid_argument si algún parámetro es inválido
 *         o si la configuración es numéricamente inestable.
 */
[[nodiscard]]
DiffusionCoefficients makeDiffusionCoefficients(
    double alpha,
    double dt,
    double dx,
    double dy
);

/**
 * @brief Calcula la temperatura de una única celda interior.
 *
 * Lee exclusivamente de current y escribe únicamente en next.
 * Esta función representa la unidad mínima de trabajo que utilizará
 * el modelo Fine-Grained.
 *
 * @param current Estado térmico actual.
 * @param next Estado térmico siguiente.
 * @param row Fila interior a actualizar.
 * @param column Columna interior a actualizar.
 * @param coefficients Coeficientes precalculados de difusión.
 */
void computeCell(
    const Grid& current,
    Grid& next,
    std::size_t row,
    std::size_t column,
    const DiffusionCoefficients& coefficients
);

/**
 * @brief Calcula un timestep completo de difusión.
 *
 * Todas las celdas interiores se calculan utilizando current.
 * Los resultados se almacenan en next.
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
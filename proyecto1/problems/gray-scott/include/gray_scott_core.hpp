#ifndef GRAY_SCOTT_CORE_HPP
#define GRAY_SCOTT_CORE_HPP

#include "grid.hpp"
#include <string>

struct GrayScottParams {
    float Du = 0.2f;    // Tasa de difusión del alimento U
    float Dv = 0.1f;    // Tasa de difusión de la bacteria V
    float F  = 0.034f;  // Tasa de alimentación (Feed)
    float K  = 0.065f;  // Tasa de mortalidad (Kill)
    float dt = 1.0f;    // Paso de tiempo discreto
};

// Inicializa la placa con alimento U=1.0 y una perturbación bacteriana central de V=1.0
void init_simulation(Grid2D& grid);

// Cálculo del stencil para un subrango continuo de filas [start_y, end_y) (Descomposición 1D)
void step_simulation_slice(const Grid2D& current, Grid2D& next, int start_y, int end_y, const GrayScottParams& params);

// Cálculo secuencial del stencil de diferencias finitas para un paso temporal
void step_simulation_sequential(const Grid2D& current, Grid2D& next, const GrayScottParams& params);

// Exporta el estado de bacterias V a una imagen PPM ultraligera
void export_frame_ppm(const Grid2D& grid, int frame_id, const std::string& output_dir);

#endif // GRAY_SCOTT_CORE_HPP
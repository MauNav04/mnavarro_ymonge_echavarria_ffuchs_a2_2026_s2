#include "sequential.hpp"
#include "heat2d.hpp"
#include <stdexcept>
#include <utility>

namespace heat2d {

/**
 * @brief Ejecuta múltiples pasos temporales utilizando un único flujo secuencial.
 *
 * En cada iteración se calcula el estado siguiente de la superficie y luego
 * se intercambian current y next. De esta manera se reutilizan las mismas
 * dos regiones de memoria durante toda la simulación.
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
) {
    if (current.rows() != next.rows() || current.cols() != next.cols()) {
        throw std::invalid_argument(
            "Current and next grids must have the same dimensions."
        );
    }

    if (steps == 0) {
        return;
    }

    if (!isStable(alpha, dt, dx, dy)) {
        throw std::invalid_argument(
            "The selected simulation parameters violate the stability condition."
        );
    }

    for (std::size_t step = 0; step < steps; ++step) {
        
        diffusionStep(
            current,
            next,
            alpha,
            dt,
            dx,
            dy,
            boundaryTemperature
        );

        std::swap(current, next);
    }
}

}  // namespace heat2d
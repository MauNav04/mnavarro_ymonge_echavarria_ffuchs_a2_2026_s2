#include "sequential.hpp"  // Incluye la declaración de la función runSequential().

#include "heat2d.hpp"  // Permite utilizar diffusionStep() para avanzar la simulación.

#include <stdexcept>  // Proporciona std::invalid_argument para reportar configuraciones inválidas.

#include <utility>  // Proporciona std::swap para intercambiar las dos mallas sin copiar sus elementos.

namespace heat2d {  // Todas las implementaciones pertenecen al namespace Heat2D.

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

    if (current.rows() != next.rows() || current.cols() != next.cols()) {  // Comprueba que ambas mallas tengan exactamente las mismas dimensiones.

        throw std::invalid_argument(  // Lanza una excepción porque las dos mallas deben representar la misma superficie.
            "Current and next grids must have the same dimensions."
        );

    }

    if (steps == 0) {  // Comprueba si el usuario solicitó ejecutar cero pasos temporales.

        return;  // Finaliza inmediatamente porque no existe trabajo que realizar.

    }

    if (!isStable(alpha, dt, dx, dy)) {  // Comprueba antes del ciclo que los parámetros produzcan una simulación numéricamente estable.

        throw std::invalid_argument(  // Detiene la simulación si el esquema explícito no cumple el criterio de estabilidad.
            "The selected simulation parameters violate the stability condition."
        );

    }

    for (std::size_t step = 0; step < steps; ++step) {  // Repite el proceso de difusión una vez por cada paso temporal solicitado.

        diffusionStep(  // Calcula el estado de la superficie correspondiente al siguiente instante temporal.
            current,  // Utiliza la malla actual únicamente como fuente de datos.
            next,  // Almacena los nuevos valores de temperatura en la malla auxiliar.
            alpha,  // Proporciona la difusividad térmica del material.
            dt,  // Proporciona el tamaño del paso temporal.
            dx,  // Proporciona la separación espacial sobre el eje X.
            dy,  // Proporciona la separación espacial sobre el eje Y.
            boundaryTemperature  // Mantiene los cuatro bordes a temperatura fija.
        );

        std::swap(current, next);  // Intercambia los roles de las dos mallas sin copiar todas sus temperaturas.

    }

}

}  // namespace heat2d
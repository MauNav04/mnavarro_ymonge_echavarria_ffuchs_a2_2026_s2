#include "heat2d.hpp"  // Incluye las declaraciones de las funciones principales de difusión.

#include "initial_conditions.hpp"  // Permite reutilizar la función que aplica condiciones de frontera fijas.

#include <stdexcept>  // Proporciona excepciones para reportar parámetros y dimensiones inválidas.

namespace heat2d {  // Todas las funciones implementadas pertenecen al namespace Heat2D.

/**
 * @brief Comprueba la condición de estabilidad del esquema explícito 2D.
 *
 * Calcula el factor:
 *
 * alpha * dt * (1 / dx^2 + 1 / dy^2)
 *
 * y verifica que su valor sea menor o igual que 0.5.
 */
bool isStable(
    double alpha,
    double dt,
    double dx,
    double dy
) {

    if (alpha <= 0.0) {  // Comprueba que la difusividad térmica sea físicamente válida.

        return false;  // Una difusividad nula o negativa no se acepta para esta simulación.

    }

    if (dt <= 0.0) {  // Comprueba que el avance temporal sea positivo.

        return false;  // Un paso temporal nulo o negativo no permite avanzar la simulación.

    }

    if (dx <= 0.0 || dy <= 0.0) {  // Comprueba que las separaciones espaciales sean positivas.

        return false;  // Una distancia espacial nula produciría además una división entre cero.

    }

    const double inverseDxSquared = 1.0 / (dx * dx);  // Calcula el término 1/dx^2 utilizado en la condición de estabilidad.

    const double inverseDySquared = 1.0 / (dy * dy);  // Calcula el término 1/dy^2 utilizado en la condición de estabilidad.

    const double stabilityFactor = alpha * dt * (inverseDxSquared + inverseDySquared);  // Calcula el factor completo del esquema explícito.

    return stabilityFactor <= 0.5;  // Indica si los parámetros cumplen la condición de estabilidad.
}

/**
 * @brief Calcula un paso temporal completo de difusión sobre la superficie.
 *
 * Cada celda interior se actualiza utilizando únicamente valores de la
 * malla current. Los resultados se escriben en next para evitar que una
 * actualización afecte los cálculos restantes de la misma iteración.
 */
void diffusionStep(
    const Grid& current,
    Grid& next,
    double alpha,
    double dt,
    double dx,
    double dy,
    double boundaryTemperature
) {

    if (current.rows() != next.rows() || current.cols() != next.cols()) {  // Comprueba que ambas mallas representen superficies del mismo tamaño.

        throw std::invalid_argument(  // Lanza una excepción porque no sería posible mapear correctamente las celdas.
            "Current and next grids must have the same dimensions."
        );

    }

    if (current.rows() < 3 || current.cols() < 3) {  // Comprueba que exista al menos una celda interior además de los bordes.

        throw std::invalid_argument(  // Informa que una malla demasiado pequeña no permite aplicar la ecuación 2D.
            "Grid must contain at least three rows and three columns."
        );

    }

    if (alpha <= 0.0) {  // Verifica que la difusividad térmica sea positiva.

        throw std::invalid_argument(  // Informa al usuario que el parámetro físico recibido es inválido.
            "Thermal diffusivity alpha must be greater than zero."
        );

    }

    if (dt <= 0.0) {  // Verifica que el paso temporal permita avanzar en el tiempo.

        throw std::invalid_argument(  // Evita ejecutar la simulación con un intervalo temporal inválido.
            "Time step dt must be greater than zero."
        );

    }

    if (dx <= 0.0 || dy <= 0.0) {  // Verifica que ambos espaciamientos físicos sean positivos.

        throw std::invalid_argument(  // Evita divisiones entre cero y configuraciones espaciales inválidas.
            "Spatial steps dx and dy must be greater than zero."
        );

    }

    if (!isStable(alpha, dt, dx, dy)) {  // Comprueba que la configuración satisfaga el criterio de estabilidad.

        throw std::invalid_argument(  // Detiene la simulación antes de producir resultados numéricamente inestables.
            "The selected alpha, dt, dx and dy values violate the stability condition."
        );

    }

    const double coefficientX = alpha * dt / (dx * dx);  // Precalcula el coeficiente asociado con la difusión en el eje X.

    const double coefficientY = alpha * dt / (dy * dy);  // Precalcula el coeficiente asociado con la difusión en el eje Y.

    for (std::size_t row = 1; row < current.rows() - 1; ++row) {  // Recorre solamente las filas interiores y excluye los bordes.

        for (std::size_t col = 1; col < current.cols() - 1; ++col) {  // Recorre solamente las columnas interiores y excluye los bordes.

            const double center = current(row, col);  // Obtiene la temperatura actual de la celda que se está procesando.

            const double left = current(row, col - 1);  // Obtiene la temperatura de la celda vecina ubicada a la izquierda.

            const double right = current(row, col + 1);  // Obtiene la temperatura de la celda vecina ubicada a la derecha.

            const double top = current(row - 1, col);  // Obtiene la temperatura de la celda vecina ubicada arriba.

            const double bottom = current(row + 1, col);  // Obtiene la temperatura de la celda vecina ubicada abajo.

            const double diffusionX = coefficientX * (right - 2.0 * center + left);  // Calcula la contribución de difusión correspondiente al eje X.

            const double diffusionY = coefficientY * (bottom - 2.0 * center + top);  // Calcula la contribución de difusión correspondiente al eje Y.

            next(row, col) = center + diffusionX + diffusionY;  // Calcula y almacena la temperatura correspondiente al siguiente instante temporal.

        }

    }

    applyFixedBoundary(next, boundaryTemperature);  // Restablece la temperatura fija de los cuatro bordes después de calcular el interior.
}

}  // namespace heat2d
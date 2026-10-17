#pragma once  // Evita que este archivo de cabecera sea incluido más de una vez durante la compilación.

#include <cstddef>  // Proporciona std::size_t para tamaños de malla y cantidad de pasos.
#include <cmath>
namespace heat2d {  // Agrupa la configuración dentro del namespace del proyecto Heat2D.

/**
 * @brief Contiene todos los parámetros necesarios para configurar una simulación Heat2D.
 *
 * Esta estructura centraliza las dimensiones físicas de la superficie,
 * resolución espacial, propiedades térmicas, condiciones iniciales y
 * parámetros temporales de la simulación.
 *
 * Mantener estos parámetros agrupados evita utilizar constantes dispersas
 * y permitirá modificar fácilmente los experimentos posteriormente.
 */
struct SimulationConfig {

    double lengthX = 1.0;  // Longitud física de la superficie sobre el eje X, expresada en metros.

    double lengthY = 1.0;  // Longitud física de la superficie sobre el eje Y, expresada en metros.

    std::size_t rows = 801;  // Cantidad total de puntos discretos utilizados sobre el eje Y.

    std::size_t cols = 801;  // Cantidad total de puntos discretos utilizados sobre el eje X.

    double alpha = 0.05;  // Difusividad térmica utilizada por el modelo de difusión.

    double dt = 0.000005;  // Tamaño de cada paso temporal, expresado en segundos (estabilidad CFL).

    double totalTime = 0.005;  // Tiempo físico total para 1000 pasos (0.005 / 0.000005 = 1000).

    double maxTemperature = 100.0;  // Temperatura máxima de la distribución sinusoidal inicial.

    double boundaryTemperature = 0.0;  // Temperatura fija mantenida en los cuatro bordes de la superficie.

    /**
     * @brief Calcula el espaciamiento físico entre columnas consecutivas.
     *
     * @return Distancia dx entre puntos consecutivos del eje X.
     */
    double dx() const {

        return lengthX / static_cast<double>(cols - 1);  // Divide la longitud física entre la cantidad real de intervalos en X.
    }

    /**
     * @brief Calcula el espaciamiento físico entre filas consecutivas.
     *
     * @return Distancia dy entre puntos consecutivos del eje Y.
     */
    double dy() const {

        return lengthY / static_cast<double>(rows - 1);  // Divide la longitud física entre la cantidad real de intervalos en Y.
    }

    [[nodiscard]]
    std::size_t steps() const {
        return static_cast<std::size_t>(
            std::llround(totalTime / dt)
        );
    }
};

}  // namespace heat2d
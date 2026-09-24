#include "config.hpp"  // Incluye la estructura con los parámetros generales de la simulación.

#include "grid.hpp"  // Incluye la representación bidimensional de las temperaturas.

#include "heat2d.hpp"  // Incluye diffusionStep() y la comprobación de estabilidad.

#include "initial_conditions.hpp"  // Incluye la inicialización sinusoidal y las condiciones de frontera.

#include <algorithm>  // Proporciona std::max para calcular errores máximos.

#include <chrono>  // Proporciona herramientas para medir únicamente el tiempo de cálculo.

#include <cmath>  // Proporciona funciones matemáticas como sin(), exp(), sqrt(), abs() e isfinite().

#include <cstddef>  // Proporciona std::size_t para índices y tamaños.

#include <iomanip>  // Permite controlar la cantidad de decimales mostrados.

#include <iostream>  // Permite imprimir resultados y mensajes de error.

#include <limits>  // Proporciona std::numeric_limits<double>::epsilon().

#include <stdexcept>  // Permite lanzar excepciones cuando una validación falla.

#include <utility>  // Proporciona std::swap para intercambiar las dos mallas.


namespace {  // Limita todos los elementos auxiliares exclusivamente a este archivo de prueba.


constexpr double PI = 3.14159265358979323846;  // Define pi con precisión suficiente para los cálculos analíticos.


/**
 * @brief Almacena las métricas de error obtenidas al comparar contra la solución analítica.
 *
 * l2Rms representa el error cuadrático medio sobre toda la malla.
 * lInfinity representa el mayor error absoluto encontrado en cualquier celda.
 */
struct AnalyticalError {

    double l2Rms = 0.0;  // Error L2 normalizado o RMS expresado en grados Celsius.

    double lInfinity = 0.0;  // Error absoluto máximo encontrado en toda la superficie.
};

/**
 * @brief Comprueba que el tiempo realmente simulado coincida con el tiempo solicitado.
 *
 * Esta validación detecta errores de redondeo o truncamiento al convertir
 * totalTime / dt en una cantidad entera de pasos temporales.
 *
 * @param requestedTime Tiempo físico solicitado por la configuración.
 * @param simulatedTime Tiempo realmente representado por steps * dt.
 */
void validateSimulatedTime(
    double requestedTime,
    double simulatedTime
) {

    const double tolerance =  // Calcula una tolerancia relacionada con la precisión del tipo double.
        100.0 *
        std::numeric_limits<double>::epsilon() *
        std::max(
            1.0,
            std::abs(requestedTime)
        );

    if (std::abs(requestedTime - simulatedTime) > tolerance) {  // Comprueba que ambos tiempos sean numéricamente equivalentes.

        throw std::runtime_error(  // Detiene la prueba si la cantidad de pasos representa un tiempo incorrecto.
            "Stress test failed: simulated time does not match requested time."
        );

    }
}

/**
 * @brief Comprueba que todas las temperaturas calculadas sean valores finitos.
 *
 * Detecta NaN, infinito positivo o infinito negativo, situaciones que normalmente
 * indicarían inestabilidad numérica, divisiones incorrectas o corrupción del cálculo.
 *
 * @param grid Malla que contiene el resultado de la simulación.
 */
void validateFiniteTemperatures(
    const heat2d::Grid& grid
) {

    for (std::size_t row = 0; row < grid.rows(); ++row) {  // Recorre todas las filas de la superficie.

        for (std::size_t col = 0; col < grid.cols(); ++col) {  // Recorre todas las columnas de la fila actual.

            if (!std::isfinite(grid(row, col))) {  // Comprueba que la temperatura sea un número finito.

                throw std::runtime_error(  // Detiene inmediatamente la prueba al encontrar un valor inválido.
                    "Stress test failed: non-finite temperature detected."
                );

            }

        }

    }

}


/**
 * @brief Comprueba que los cuatro bordes mantengan exactamente la condición de Dirichlet esperada.
 *
 * Se utiliza una tolerancia de 1e-9 para evitar depender de comparaciones excesivamente
 * estrictas de punto flotante y, al mismo tiempo, detectar cualquier modificación real
 * de las condiciones de frontera.
 *
 * @param grid Malla que se desea validar.
 * @param expectedTemperature Temperatura impuesta en los cuatro bordes.
 */
void validateBoundaries(
    const heat2d::Grid& grid,
    double expectedTemperature
) {

    constexpr double tolerance = 1e-9;  // Utiliza una tolerancia segura para comparaciones de double.

    for (std::size_t col = 0; col < grid.cols(); ++col) {  // Recorre horizontalmente los bordes superior e inferior.

        if (std::abs(grid(0, col) - expectedTemperature) > tolerance) {  // Comprueba el borde superior.

            throw std::runtime_error(  // Informa que la frontera superior fue modificada incorrectamente.
                "Stress test failed: top boundary changed."
            );

        }

        if (std::abs(
                grid(grid.rows() - 1, col) -
                expectedTemperature
            ) > tolerance) {  // Comprueba el borde inferior.

            throw std::runtime_error(  // Informa que la frontera inferior fue modificada incorrectamente.
                "Stress test failed: bottom boundary changed."
            );

        }

    }

    for (std::size_t row = 0; row < grid.rows(); ++row) {  // Recorre verticalmente los bordes izquierdo y derecho.

        if (std::abs(grid(row, 0) - expectedTemperature) > tolerance) {  // Comprueba el borde izquierdo.

            throw std::runtime_error(  // Informa que la frontera izquierda fue modificada incorrectamente.
                "Stress test failed: left boundary changed."
            );

        }

        if (std::abs(
                grid(row, grid.cols() - 1) -
                expectedTemperature
            ) > tolerance) {  // Comprueba el borde derecho.

            throw std::runtime_error(  // Informa que la frontera derecha fue modificada incorrectamente.
                "Stress test failed: right boundary changed."
            );

        }

    }

}


/**
 * @brief Comprueba que todas las temperaturas permanezcan dentro del rango físico esperado.
 *
 * Para la condición inicial positiva utilizada y bordes en cero, el principio máximo
 * de la ecuación del calor indica que no deberían aparecer temperaturas menores que
 * cero ni superiores al máximo inicial.
 *
 * @param grid Malla que se desea validar.
 * @param maximumTemperature Temperatura máxima permitida.
 */
void validateTemperatureRange(
    const heat2d::Grid& grid,
    double maximumTemperature
) {

    constexpr double tolerance = 1e-9;  // Permite pequeñas diferencias debidas a precisión de punto flotante.

    for (std::size_t row = 0; row < grid.rows(); ++row) {  // Recorre todas las filas.

        for (std::size_t col = 0; col < grid.cols(); ++col) {  // Recorre todas las columnas.

            const double temperature = grid(row, col);  // Obtiene la temperatura de la celda actual.

            if (temperature < -tolerance) {  // Comprueba que no aparezcan temperaturas negativas.

                throw std::runtime_error(  // Detiene la prueba porque el resultado viola el rango esperado.
                    "Stress test failed: negative temperature detected."
                );

            }

            if (temperature > maximumTemperature + tolerance) {  // Comprueba que no aparezca generación artificial de calor.

                throw std::runtime_error(  // Detiene la prueba si se supera el máximo de la condición inicial.
                    "Stress test failed: temperature exceeded initial maximum."
                );

            }

        }

    }

}


/**
 * @brief Comprueba la simetría espacial de la solución numérica.
 *
 * La distribución sinusoidal inicial y las condiciones de frontera son simétricas.
 * Por ello, la solución debe conservar simetría respecto al centro de la superficie.
 *
 * Cada punto (row, col) se compara contra:
 *
 * (rows - 1 - row, cols - 1 - col)
 *
 * Esto permite detectar errores de índices, desplazamientos off-by-one o actualizaciones
 * incorrectas de regiones específicas de la malla.
 *
 * @param grid Malla que se desea verificar.
 */
void validateSpatialSymmetry(
    const heat2d::Grid& grid
) {

    constexpr double tolerance = 1e-9;  // Define la diferencia máxima aceptada entre puntos simétricos.

    double maximumSymmetryError = 0.0;  // Guarda la mayor diferencia encontrada durante toda la validación.

    for (std::size_t row = 0; row < grid.rows(); ++row) {  // Recorre todas las filas.

        const std::size_t symmetricRow =  // Calcula la fila reflejada respecto al centro.
            grid.rows() - 1 - row;

        for (std::size_t col = 0; col < grid.cols(); ++col) {  // Recorre todas las columnas.

            const std::size_t symmetricCol =  // Calcula la columna reflejada respecto al centro.
                grid.cols() - 1 - col;

            const double difference = std::abs(  // Calcula la diferencia entre la celda y su equivalente simétrica.
                grid(row, col) -
                grid(symmetricRow, symmetricCol)
            );

            maximumSymmetryError = std::max(  // Conserva el mayor error de simetría encontrado.
                maximumSymmetryError,
                difference
            );

        }

    }

    if (maximumSymmetryError > tolerance) {  // Comprueba que la peor diferencia se mantenga dentro del límite.

        throw std::runtime_error(  // Informa de un posible error de índices o actualización espacial.
            "Stress test failed: spatial symmetry was not preserved."
        );

    }

}


/**
 * @brief Calcula la suma total de temperaturas almacenadas en la malla.
 *
 * Esta suma se utiliza como una medida discreta proporcional al contenido térmico
 * total de la superficie. Con bordes de Dirichlet en cero, debe disminuir a medida
 * que el calor abandona el dominio.
 *
 * @param grid Malla cuya suma térmica se desea calcular.
 * @return Suma de las temperaturas de todas las celdas.
 */
double calculateTotalTemperature(
    const heat2d::Grid& grid
) {

    double totalTemperature = 0.0;  // Inicializa el acumulador de temperatura total.

    for (std::size_t row = 0; row < grid.rows(); ++row) {  // Recorre todas las filas de la superficie.

        for (std::size_t col = 0; col < grid.cols(); ++col) {  // Recorre todas las columnas.

            totalTemperature += grid(row, col);  // Agrega la temperatura de la celda actual al acumulador.

        }

    }

    return totalTemperature;  // Devuelve la suma térmica total obtenida.
}


/**
 * @brief Comprueba que la energía térmica discreta disminuya entre dos pasos consecutivos.
 *
 * Se incluye una tolerancia relacionada con epsilon de double para evitar que pequeñas
 * diferencias de redondeo sean interpretadas como violaciones físicas.
 *
 * @param previousEnergy Energía térmica del paso anterior.
 * @param currentEnergy Energía térmica del nuevo paso.
 * @param grid Malla utilizada para estimar una tolerancia proporcional al problema.
 * @param maximumTemperature Escala máxima de temperatura de la simulación.
 */
void validateEnergyDecrease(
    double previousEnergy,
    double currentEnergy,
    const heat2d::Grid& grid,
    double maximumTemperature
) {

    const double tolerance =  // Calcula una tolerancia relacionada con la precisión de double y el tamaño del problema.
        100.0 *
        std::numeric_limits<double>::epsilon() *
        static_cast<double>(grid.size()) *
        std::max(1.0, maximumTemperature);

    if (currentEnergy >= previousEnergy - tolerance) {  // Comprueba que exista una disminución real por encima del ruido numérico.

        throw std::runtime_error(  // Informa de una posible generación artificial de energía.
            "Stress test failed: total thermal energy did not decrease."
        );

    }

}


/**
 * @brief Calcula la temperatura analítica exacta para una coordenada y tiempo determinados.
 *
 * La condición inicial sinusoidal es un modo propio de la ecuación del calor.
 * Para una superficie rectangular con bordes en cero, su evolución exacta es:
 *
 * T(x,y,t) =
 * Tmax *
 * sin(pi*x/Lx) *
 * sin(pi*y/Ly) *
 * exp[-alpha*pi^2*(1/Lx^2 + 1/Ly^2)*t]
 *
 * Para Lx = Ly = 1, esta expresión se reduce exactamente a:
 *
 * Tmax * sin(pi*x) * sin(pi*y) * exp(-2*pi^2*alpha*t)
 *
 * @param x Coordenada física sobre el eje X.
 * @param y Coordenada física sobre el eje Y.
 * @param time Tiempo físico de evaluación.
 * @param config Parámetros utilizados por la simulación.
 * @return Temperatura analítica exacta en el punto indicado.
 */
double analyticalTemperature(
    double x,
    double y,
    double time,
    const heat2d::SimulationConfig& config
) {

    const double spatialX =  // Calcula la componente espacial correspondiente al eje X.
        std::sin(PI * x / config.lengthX);

    const double spatialY =  // Calcula la componente espacial correspondiente al eje Y.
        std::sin(PI * y / config.lengthY);

    const double decayExponent =  // Calcula el exponente que describe el decaimiento temporal exacto.
        -config.alpha *
        PI *
        PI *
        (
            1.0 / (config.lengthX * config.lengthX) +
            1.0 / (config.lengthY * config.lengthY)
        ) *
        time;

    return  // Combina las componentes espaciales y temporales de la solución analítica.
        config.maxTemperature *
        spatialX *
        spatialY *
        std::exp(decayExponent);
}


/**
 * @brief Calcula los errores L2 RMS y L infinito contra la solución analítica.
 *
 * La comparación se realiza sobre cada punto de la malla utilizando el tiempo
 * realmente simulado, es decir, steps * dt.
 *
 * @param grid Resultado numérico final.
 * @param config Configuración utilizada durante la simulación.
 * @param simulatedTime Tiempo físico realmente alcanzado.
 * @return Estructura que contiene los errores L2 RMS y L infinito.
 */
AnalyticalError calculateAnalyticalError(
    const heat2d::Grid& grid,
    const heat2d::SimulationConfig& config,
    double simulatedTime
) {

    double squaredErrorSum = 0.0;  // Acumula el cuadrado del error de todas las celdas.

    double maximumError = 0.0;  // Mantiene el mayor error absoluto encontrado.

    for (std::size_t row = 0; row < grid.rows(); ++row) {  // Recorre todas las filas.

        const double y =  // Convierte el índice de fila en una coordenada física Y.
            static_cast<double>(row) *
            config.dy();

        for (std::size_t col = 0; col < grid.cols(); ++col) {  // Recorre todas las columnas.

            const double x =  // Convierte el índice de columna en una coordenada física X.
                static_cast<double>(col) *
                config.dx();

            const double exactTemperature = analyticalTemperature(  // Calcula la solución matemática exacta en esta celda.
                x,
                y,
                simulatedTime,
                config
            );

            const double numericalTemperature = grid(row, col);  // Obtiene la solución producida por diferencias finitas.

            const double error =  // Calcula el error absoluto firmado de la celda.
                numericalTemperature -
                exactTemperature;

            squaredErrorSum += error * error;  // Agrega el error cuadrático al acumulador L2.

            maximumError = std::max(  // Actualiza la norma L infinito si esta celda tiene un error mayor.
                maximumError,
                std::abs(error)
            );

        }

    }

    AnalyticalError result;  // Crea la estructura que almacenará las dos métricas.

    result.l2Rms = std::sqrt(  // Calcula el error cuadrático medio sobre todas las celdas.
        squaredErrorSum /
        static_cast<double>(grid.size())
    );

    result.lInfinity = maximumError;  // Guarda directamente el mayor error absoluto encontrado.

    return result;  // Devuelve ambas métricas de error.
}


/**
 * @brief Comprueba que el resultado numérico se mantenga suficientemente cerca de la solución exacta.
 *
 * Los umbrales utilizados son deliberadamente mucho mayores que el error esperado para
 * esta configuración, pero suficientemente pequeños para detectar errores importantes
 * en la discretización, índices, coeficientes o avance temporal.
 *
 * @param error Métricas calculadas contra la solución analítica.
 */
void validateAnalyticalError(
    const AnalyticalError& error
) {

    constexpr double maximumL2RmsError = 5e-4;  // Establece el error RMS máximo permitido en grados Celsius.

    constexpr double maximumLInfinityError = 1e-3;  // Establece el error absoluto máximo permitido en cualquier celda.

    if (error.l2Rms > maximumL2RmsError) {  // Comprueba que el error global permanezca dentro del límite.

        throw std::runtime_error(  // Informa que la solución numérica se alejó demasiado de la solución exacta.
            "Stress test failed: L2 RMS analytical error exceeded tolerance."
        );

    }

    if (error.lInfinity > maximumLInfinityError) {  // Comprueba el peor punto individual de toda la superficie.

        throw std::runtime_error(  // Informa que al menos una celda tiene un error excesivo.
            "Stress test failed: L-infinity analytical error exceeded tolerance."
        );

    }

}


}  // namespace


/**
 * @brief Ejecuta una prueba robusta y pesada sobre la implementación secuencial Heat2D.
 *
 * La prueba verifica estabilidad, finitud, condiciones de frontera, rango de
 * temperaturas, simetría espacial, decrecimiento de energía en cada paso y
 * convergencia respecto a la solución analítica conocida.
 *
 * El tiempo reportado acumula únicamente el tiempo consumido por diffusionStep(),
 * excluyendo las validaciones adicionales realizadas entre pasos.
 *
 * @return 0 si todas las validaciones se completan correctamente.
 * @return 1 si alguna validación o parte de la simulación falla.
 */
int main() {

    try {  // Inicia un bloque protegido para capturar errores durante la prueba.

        heat2d::SimulationConfig config;  // Crea una configuración independiente para la prueba de estrés.

        config.lengthX = 1.0;  // Define una superficie de un metro sobre el eje X.

        config.lengthY = 1.0;  // Define una superficie de un metro sobre el eje Y.

        config.rows = 801;  // Utiliza 801 puntos sobre el eje Y.

        config.cols = 801;  // Utiliza 801 puntos sobre el eje X.

        config.alpha = 0.05;  // Utiliza la misma difusividad de la configuración base.

        config.dt = 0.000005;  // Utiliza un paso temporal estable para la resolución seleccionada.

        config.totalTime = 0.005;  // Ejecuta exactamente 1000 pasos temporales.

        config.maxTemperature = 100.0;  // Define la amplitud máxima de la condición inicial.

        config.boundaryTemperature = 0.0;  // Mantiene los cuatro bordes en cero grados.

        std::cout << "\n=== Heat2D Sequential Robust Stress Test ===\n";  // Muestra el encabezado de la prueba.

        std::cout << std::fixed << std::setprecision(9);  // Utiliza suficiente precisión para observar errores numéricos pequeños.

        std::cout << "Grid: "
                  << config.rows
                  << " x "
                  << config.cols
                  << "\n";  // Muestra la resolución utilizada.

        std::cout << "dx: "
                  << config.dx()
                  << " m\n";  // Muestra la separación espacial horizontal.

        std::cout << "dy: "
                  << config.dy()
                  << " m\n";  // Muestra la separación espacial vertical.

        std::cout << "dt: "
                  << config.dt
                  << " s\n";  // Muestra el paso temporal.

        std::cout << "Steps: "
                  << config.steps()
                  << "\n";  // Muestra la cantidad de iteraciones.

        if (!heat2d::isStable(  // Verifica el criterio de estabilidad antes de ejecutar la prueba.
                config.alpha,
                config.dt,
                config.dx(),
                config.dy()
            )) {

            throw std::runtime_error(  // Evita ejecutar una configuración numéricamente inválida.
                "Stress test configuration is numerically unstable."
            );

        }

        std::cout << "Stability: STABLE\n";  // Confirma que el esquema cumple la condición requerida.

        heat2d::Grid current(  // Crea la malla que almacena el estado actual.
            config.rows,
            config.cols,
            config.boundaryTemperature
        );

        heat2d::Grid next(  // Crea la malla auxiliar para el siguiente estado.
            config.rows,
            config.cols,
            config.boundaryTemperature
        );

        heat2d::initializeSinusoidal(  // Genera la condición inicial exactamente compatible con la solución analítica.
            current,
            config.maxTemperature,
            config.lengthX,
            config.lengthY
        );

        heat2d::applyFixedBoundary(  // Fuerza explícitamente los cuatro bordes iniciales.
            current,
            config.boundaryTemperature
        );

        validateFiniteTemperatures(current);  // Comprueba la validez numérica de la condición inicial.

        validateBoundaries(  // Comprueba que los cuatro bordes comiencen correctamente.
            current,
            config.boundaryTemperature
        );

        validateSpatialSymmetry(current);  // Comprueba que la condición inicial sea espacialmente simétrica.

        const std::size_t centerRow = config.rows / 2;  // Calcula la fila central.

        const std::size_t centerCol = config.cols / 2;  // Calcula la columna central.

        const double initialCenterTemperature = current(  // Guarda la temperatura inicial del centro.
            centerRow,
            centerCol
        );

        const double initialEnergy = calculateTotalTemperature(current);  // Calcula la suma térmica inicial.

        double previousEnergy = initialEnergy;  // Inicializa la referencia utilizada para verificar monotonicidad.

        double accumulatedDiffusionSeconds = 0.0;  // Acumula únicamente el tiempo empleado por diffusionStep().

        for (std::size_t step = 0; step < config.steps(); ++step) {  // Ejecuta cada paso temporal de manera observable.

            const auto stepStart =  // Registra el instante inmediatamente anterior al cálculo físico.
                std::chrono::steady_clock::now();

            heat2d::diffusionStep(  // Calcula exactamente un paso temporal de difusión.
                current,
                next,
                config.alpha,
                config.dt,
                config.dx(),
                config.dy(),
                config.boundaryTemperature
            );

            const auto stepEnd =  // Registra el instante inmediatamente posterior al cálculo físico.
                std::chrono::steady_clock::now();

            accumulatedDiffusionSeconds +=  // Agrega únicamente el tiempo consumido por el kernel de difusión.
                std::chrono::duration<double>(
                    stepEnd - stepStart
                ).count();

            const double nextEnergy = calculateTotalTemperature(next);  // Calcula el contenido térmico después del nuevo paso.

            validateEnergyDecrease(  // Comprueba que no haya generación artificial de calor.
                previousEnergy,
                nextEnergy,
                next,
                config.maxTemperature
            );

            std::swap(current, next);  // Convierte el estado recién calculado en el nuevo estado actual.

            previousEnergy = nextEnergy;  // Conserva la energía para compararla contra el próximo paso.
        }

        const double simulatedTime =  // Calcula el tiempo físico realmente alcanzado.
            static_cast<double>(config.steps()) *
            config.dt;
            validateSimulatedTime(  // Comprueba que la cantidad de pasos haya representado correctamente el tiempo solicitado.
                config.totalTime,
                simulatedTime
            );

        const double finalCenterTemperature = current(  // Obtiene la temperatura central final.
            centerRow,
            centerCol
        );

        const double finalEnergy = calculateTotalTemperature(current);  // Calcula la suma térmica final.

        validateFiniteTemperatures(current);  // Comprueba que no exista NaN ni infinito después del estrés.

        validateBoundaries(  // Verifica que las fronteras permanezcan exactamente controladas.
            current,
            config.boundaryTemperature
        );

        validateTemperatureRange(  // Verifica el principio máximo esperado.
            current,
            config.maxTemperature
        );

        validateSpatialSymmetry(current);  // Comprueba que la simetría siga presente después de todos los pasos.

        if (finalCenterTemperature >= initialCenterTemperature) {  // Verifica que el punto inicialmente más caliente se enfríe.

            throw std::runtime_error(
                "Stress test failed: center temperature did not decrease."
            );

        }

        const AnalyticalError analyticalError = calculateAnalyticalError(  // Compara cada celda contra la solución exacta.
            current,
            config,
            simulatedTime
        );

        validateAnalyticalError(analyticalError);  // Falla la prueba si el error numérico supera los límites aceptados.

        const std::size_t interiorCells =  // Calcula la cantidad de celdas que realmente actualiza diffusionStep().
            (config.rows - 2) *
            (config.cols - 2);

        const double totalCellUpdates =  // Calcula la cantidad total de actualizaciones realizadas.
            static_cast<double>(interiorCells) *
            static_cast<double>(config.steps());

        const double updatesPerSecond =  // Calcula el throughput usando solamente tiempo del kernel de difusión.
            totalCellUpdates /
            accumulatedDiffusionSeconds;

        const double energyReductionPercentage =  // Calcula el porcentaje total de energía térmica perdida.
            100.0 *
            (initialEnergy - finalEnergy) /
            initialEnergy;

        std::cout << "\n--- Numerical Results ---\n";  // Separa visualmente los resultados físicos.

        std::cout << "Initial center temperature: "
                  << initialCenterTemperature
                  << " C\n";

        std::cout << "Final center temperature: "
                  << finalCenterTemperature
                  << " C\n";

        std::cout << "Initial thermal sum: "
                  << initialEnergy
                  << "\n";

        std::cout << "Final thermal sum: "
                  << finalEnergy
                  << "\n";

        std::cout << "Thermal sum reduction: "
                  << energyReductionPercentage
                  << " %\n";

        std::cout << "L2 RMS analytical error: "
                  << analyticalError.l2Rms
                  << " C\n";

        std::cout << "L-infinity analytical error: "
                  << analyticalError.lInfinity
                  << " C\n";

        std::cout << "\n--- Performance Information ---\n";  // Separa la información de rendimiento.

        std::cout << "Diffusion kernel time: "
                  << accumulatedDiffusionSeconds
                  << " s\n";

        std::cout << "Approximate cell updates: "
                  << totalCellUpdates
                  << "\n";

        std::cout << "Approximate updates per second: "
                  << updatesPerSecond
                  << "\n";

        std::cout << "\nValidations:\n";  // Resume las comprobaciones realizadas.

        std::cout << "  [PASS] Numerical stability\n";

        std::cout << "  [PASS] Finite temperatures\n";

        std::cout << "  [PASS] Dirichlet boundaries\n";

        std::cout << "  [PASS] Physical temperature range\n";

        std::cout << "  [PASS] Spatial symmetry\n";

        std::cout << "  [PASS] Thermal energy decreases every step\n";

        std::cout << "  [PASS] Analytical L2 error\n";

        std::cout << "  [PASS] Analytical L-infinity error\n";

        std::cout << "  [PASS] Requested simulation time\n";

        std::cout << "\nStress test: PASSED\n\n";  // Confirma que todas las comprobaciones fueron superadas.

        return 0;  // Finaliza indicando éxito.

    } catch (const std::exception& error) {  // Captura cualquier problema detectado durante la prueba.

        std::cerr << "\nStress test: FAILED\n";  // Informa claramente que alguna comprobación falló.

        std::cerr << "Reason: "
                  << error.what()
                  << "\n";  // Muestra la causa específica.

        return 1;  // Devuelve un código de error al sistema operativo.
    }
}
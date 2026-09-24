#include "config.hpp"  // Incluye la estructura que almacena todos los parámetros de la simulación.

#include "grid.hpp"  // Incluye la clase utilizada para representar las superficies de temperatura.

#include "heat2d.hpp"  // Incluye la función que permite verificar la estabilidad numérica.

#include "initial_conditions.hpp"  // Incluye las funciones utilizadas para crear la distribución inicial y los bordes.

#include "sequential.hpp"  // Incluye la implementación del modelo de ejecución secuencial.

#include <exception>  // Proporciona std::exception para capturar errores producidos durante la simulación.

#include <iomanip>  // Proporciona std::fixed y std::setprecision para controlar la presentación de números.

#include <iostream>  // Proporciona std::cout y std::cerr para mostrar información en la terminal.

namespace {  // Limita las siguientes funciones auxiliares exclusivamente a este archivo.

/**
 * @brief Muestra en terminal los parámetros principales utilizados por la simulación.
 *
 * @param config Configuración que contiene las dimensiones físicas,
 * resolución y parámetros temporales del experimento.
 */
void printConfiguration(
    const heat2d::SimulationConfig& config
) {

    std::cout << "\n=== Heat Diffusion 2D - Sequential Simulation ===\n";  // Muestra el encabezado principal del programa.

    std::cout << std::fixed << std::setprecision(6);  // Configura la salida numérica con seis posiciones decimales.

    std::cout << "Surface size: "
              << config.lengthX
              << " m x "
              << config.lengthY
              << " m\n";  // Muestra las dimensiones físicas de la superficie.

    std::cout << "Grid size: "
              << config.rows
              << " x "
              << config.cols
              << "\n";  // Muestra la cantidad de puntos discretos utilizados.

    std::cout << "dx: "
              << config.dx()
              << " m\n";  // Muestra la separación espacial entre columnas consecutivas.

    std::cout << "dy: "
              << config.dy()
              << " m\n";  // Muestra la separación espacial entre filas consecutivas.

    std::cout << "Alpha: "
              << config.alpha
              << "\n";  // Muestra la difusividad térmica utilizada en la simulación.

    std::cout << "dt: "
              << config.dt
              << " s\n";  // Muestra el tamaño de cada paso temporal.

    std::cout << "Requested simulation time: "
              << config.totalTime
              << " s\n";  // Muestra el tiempo físico que se desea simular.

    std::cout << "Time steps: "
              << config.steps()
              << "\n";  // Muestra la cantidad total de iteraciones temporales.

    std::cout << "Actually simulated time: "
              << static_cast<double>(config.steps()) * config.dt
              << " s\n";  // Muestra el tiempo realmente representado por la cantidad entera de pasos.

}

/**
 * @brief Muestra el estado de estabilidad numérica de la configuración.
 *
 * @param config Configuración utilizada por la simulación.
 */
void printStability(
    const heat2d::SimulationConfig& config
) {

    const bool stable = heat2d::isStable(  // Comprueba la condición de estabilidad del esquema explícito.
        config.alpha,  // Envía la difusividad térmica.
        config.dt,  // Envía el tamaño del paso temporal.
        config.dx(),  // Envía el espaciamiento horizontal.
        config.dy()  // Envía el espaciamiento vertical.
    );

    std::cout << "Numerical stability: "
              << (stable ? "STABLE" : "UNSTABLE")
              << "\n";  // Muestra de forma legible si la configuración puede ejecutarse de manera estable.
}
} //namespace   

/**
 * @brief Punto de entrada principal de la simulación secuencial Heat2D.
 *
 * Crea las mallas, genera la condición inicial, ejecuta todos los pasos
 * temporales y muestra la evolución de la temperatura en el centro.
 *
 * @return 0 si la simulación termina correctamente y 1 si ocurre un error.
 */
int main() {

    try {  // Inicia un bloque protegido para capturar posibles errores de configuración o simulación.

        const heat2d::SimulationConfig config;  // Crea la configuración utilizando los valores predeterminados definidos en config.hpp.

        printConfiguration(config);  // Muestra los parámetros utilizados antes de comenzar la simulación.

        printStability(config);  // Comprueba y muestra si los parámetros cumplen la condición de estabilidad.

        heat2d::Grid current(  // Crea la malla que representará el estado actual de la superficie.
            config.rows,  // Define la cantidad de filas de la superficie.
            config.cols,  // Define la cantidad de columnas de la superficie.
            config.boundaryTemperature  // Inicializa todas las celdas con la temperatura de frontera.
        );

        heat2d::Grid next(  // Crea una segunda malla auxiliar utilizada para almacenar el siguiente estado temporal.
            config.rows,  // Utiliza exactamente la misma cantidad de filas que current.
            config.cols,  // Utiliza exactamente la misma cantidad de columnas que current.
            config.boundaryTemperature  // Inicializa inicialmente todas las celdas con la temperatura de frontera.
        );

        heat2d::initializeSinusoidal(  // Genera la distribución sinusoidal inicial sobre la superficie.
            current,  // Modifica la malla correspondiente al estado inicial.
            config.maxTemperature,  // Define la temperatura máxima de la distribución.
            config.lengthX,  // Proporciona la longitud física sobre el eje X.
            config.lengthY  // Proporciona la longitud física sobre el eje Y.
        );

        heat2d::applyFixedBoundary(  // Garantiza explícitamente que los cuatro bordes comiencen con la temperatura especificada.
            current,  // Aplica la condición sobre la malla inicial.
            config.boundaryTemperature  // Define la temperatura fija utilizada en los bordes.
        );

        const std::size_t centerRow = config.rows / 2;  // Calcula la fila correspondiente aproximadamente al centro de la superficie.

        const std::size_t centerCol = config.cols / 2;  // Calcula la columna correspondiente aproximadamente al centro de la superficie.

        const double initialCenterTemperature = current(  // Lee la temperatura inicial ubicada en el centro de la superficie.
            centerRow,  // Selecciona la fila central.
            centerCol  // Selecciona la columna central.
        );

        std::cout << "Initial center temperature: "
                  << initialCenterTemperature
                  << " C\n";  // Muestra la temperatura central antes de comenzar la difusión.

        heat2d::runSequential(  // Ejecuta la simulación completa utilizando únicamente el modelo secuencial.
            current,  // Proporciona la malla que contiene inicialmente las temperaturas actuales.
            next,  // Proporciona la malla auxiliar utilizada entre pasos temporales.
            config.steps(),  // Indica cuántas iteraciones temporales se deben calcular.
            config.alpha,  // Proporciona la difusividad térmica del modelo.
            config.dt,  // Proporciona el tamaño del paso temporal.
            config.dx(),  // Proporciona el espaciamiento espacial horizontal.
            config.dy(),  // Proporciona el espaciamiento espacial vertical.
            config.boundaryTemperature  // Mantiene los cuatro bordes a temperatura fija.
        );

        const double finalCenterTemperature = current(  // Lee la temperatura del centro después de completar todos los pasos.
            centerRow,  // Selecciona la misma fila central utilizada inicialmente.
            centerCol  // Selecciona la misma columna central utilizada inicialmente.
        );

        std::cout << "Final center temperature: "
                  << finalCenterTemperature
                  << " C\n";  // Muestra la temperatura obtenida después de la difusión.

        std::cout << "Simulation completed successfully.\n\n";  // Informa que toda la ejecución terminó sin errores.

        return 0;  // Finaliza el programa indicando una ejecución exitosa.

    } catch (const std::exception& error) {  // Captura cualquier excepción estándar generada por los módulos de la simulación.

        std::cerr << "Simulation error: "
                  << error.what()
                  << "\n";  // Muestra en terminal la descripción específica del error ocurrido.

        return 1;  // Finaliza el programa indicando que ocurrió un error.
    }
}
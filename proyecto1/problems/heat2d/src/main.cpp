#include "config.hpp"
#include "grid.hpp"
#include "heat2d.hpp"
#include "initial_conditions.hpp"
#include "sequential.hpp"

#include <exception>
#include <iomanip>
#include <iostream>

namespace {

/**
 * @brief Muestra en terminal los parámetros principales utilizados por la simulación.
 *
 * @param config Configuración que contiene las dimensiones físicas,
 * resolución y parámetros temporales del experimento.
 */
void printConfiguration(
    const heat2d::SimulationConfig& config
) {
    std::cout << "\n=== Heat Diffusion 2D - Sequential Simulation ===\n";
    std::cout << std::fixed << std::setprecision(6);

    std::cout << "Surface size: "
              << config.lengthX
              << " m x "
              << config.lengthY
              << " m\n";

    std::cout << "Grid size: "
              << config.rows
              << " x "
              << config.cols
              << "\n";

    std::cout << "dx: "
              << config.dx()
              << " m\n";

    std::cout << "dy: "
              << config.dy()
              << " m\n";

    std::cout << "Alpha: "
              << config.alpha
              << "\n";

    std::cout << "dt: "
              << config.dt
              << " s\n";

    std::cout << "Requested simulation time: "
              << config.totalTime
              << " s\n";

    std::cout << "Time steps: "
              << config.steps()
              << "\n";

    std::cout << "Actually simulated time: "
              << static_cast<double>(config.steps()) * config.dt
              << " s\n";
}

/**
 * @brief Muestra el estado de estabilidad numérica de la configuración.
 *
 * @param config Configuración utilizada por la simulación.
 */
void printStability(
    const heat2d::SimulationConfig& config
) {
    const bool stable = heat2d::isStable(
        config.alpha,
        config.dt,
        config.dx(),
        config.dy()
    );

    std::cout << "Numerical stability: "
              << (stable ? "STABLE" : "UNSTABLE")
              << "\n";
}

} // namespace

/**
 * @brief Punto de entrada principal de la simulación secuencial Heat2D.
 *
 * Crea las mallas, genera la condición inicial, ejecuta todos los pasos
 * temporales y muestra la evolución de la temperatura en el centro.
 *
 * @return 0 si la simulación termina correctamente y 1 si ocurre un error.
 */
int main() {
    try {
        const heat2d::SimulationConfig config;

        printConfiguration(config);
        printStability(config);

        heat2d::Grid current(
            config.rows,
            config.cols,
            config.boundaryTemperature
        );

        heat2d::Grid next(
            config.rows,
            config.cols,
            config.boundaryTemperature
        );

        heat2d::initializeSinusoidal(
            current,
            config.maxTemperature,
            config.lengthX,
            config.lengthY
        );

        heat2d::applyFixedBoundary(
            current,
            config.boundaryTemperature
        );

        const std::size_t centerRow = config.rows / 2;
        const std::size_t centerCol = config.cols / 2;

        const double initialCenterTemperature = current(centerRow, centerCol);

        std::cout << "Initial center temperature: "
                  << initialCenterTemperature
                  << " C\n";

        heat2d::runSequential(
            current,
            next,
            config.steps(),
            config.alpha,
            config.dt,
            config.dx(),
            config.dy(),
            config.boundaryTemperature
        );

        const double finalCenterTemperature = current(centerRow, centerCol);

        std::cout << "Final center temperature: "
                  << finalCenterTemperature
                  << " C\n";

        std::cout << "Simulation completed successfully.\n\n";

        return 0;

    } catch (const std::exception& error) {
        std::cerr << "Simulation error: "
                  << error.what()
                  << "\n";
        return 1;
    }
}
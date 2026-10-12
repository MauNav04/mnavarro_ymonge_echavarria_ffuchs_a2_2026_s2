#include "config.hpp"
#include "fine_grained/fine_grained.hpp"
#include "grid.hpp"
#include "heat2d.hpp"
#include "initial_conditions.hpp"

#include <chrono>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

constexpr std::size_t defaultLogicalThreadCount = 4;

/**
 * @brief Obtiene la cantidad de hilos lógicos desde la línea de comandos.
 *
 * Si no se especifica un valor, utiliza cuatro contextos lógicos.
 */
std::size_t parseLogicalThreadCount(
    int argc,
    char* argv[]
) {
    if (argc == 1) {
        return defaultLogicalThreadCount;
    }

    if (argc != 2) {
        throw std::invalid_argument(
            "Usage: ./heat2d_fine_grained [logical_thread_count]"
        );
    }

    const std::string argument = argv[1];

    std::size_t processedCharacters = 0;

    const unsigned long long parsedValue =
        std::stoull(
            argument,
            &processedCharacters
        );

    if (processedCharacters != argument.size()) {
        throw std::invalid_argument(
            "Logical thread count must be a positive integer."
        );
    }

    if (parsedValue == 0) {
        throw std::invalid_argument(
            "Logical thread count must be greater than zero."
        );
    }

    return static_cast<std::size_t>(
        parsedValue
    );
}

/**
 * @brief Imprime la configuración principal del experimento.
 */
void printConfiguration(
    const heat2d::SimulationConfig& config,
    std::size_t logicalThreadCount
) {
    std::cout
        << "\n=== Heat Diffusion 2D - Fine-Grained ===\n";

    std::cout
        << std::fixed
        << std::setprecision(6);

    std::cout
        << "Surface size: "
        << config.lengthX
        << " m x "
        << config.lengthY
        << " m\n";

    std::cout
        << "Grid size: "
        << config.rows
        << " x "
        << config.cols
        << '\n';

    std::cout
        << "dx: "
        << config.dx()
        << " m\n";

    std::cout
        << "dy: "
        << config.dy()
        << " m\n";

    std::cout
        << "Alpha: "
        << config.alpha
        << '\n';

    std::cout
        << "dt: "
        << config.dt
        << " s\n";

    std::cout
        << "Time steps: "
        << config.steps()
        << '\n';

    std::cout
        << "Logical threads: "
        << logicalThreadCount
        << '\n';

    std::cout
        << "Scheduling: Round-Robin\n";

    std::cout
        << "Quantum size: 1 cell\n";
}

/**
 * @brief Calcula la cantidad teórica de quantums de la simulación.
 */
std::size_t expectedQuanta(
    const heat2d::SimulationConfig& config
) {
    const std::size_t interiorCells =
        (config.rows - 2) *
        (config.cols - 2);

    return interiorCells *
           config.steps();
}

}  // namespace

int main(
    int argc,
    char* argv[]
) {
    try {
        const std::size_t logicalThreadCount =
            parseLogicalThreadCount(
                argc,
                argv
            );

        heat2d::SimulationConfig config;

        printConfiguration(
            config,
            logicalThreadCount
        );

        if (
            !heat2d::isStable(
                config.alpha,
                config.dt,
                config.dx(),
                config.dy()
            )
        ) {
            throw std::runtime_error(
                "Simulation configuration is numerically unstable."
            );
        }

        std::cout
            << "Numerical stability: STABLE\n";

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

        const std::size_t centerRow =
            config.rows / 2;

        const std::size_t centerColumn =
            config.cols / 2;

        const double initialCenterTemperature =
            current(
                centerRow,
                centerColumn
            );

        const auto startTime =
            std::chrono::steady_clock::now();

        const heat2d::fine_grained::FineGrainedStats stats =
            heat2d::fine_grained::runFineGrained(
                current,
                next,
                config.steps(),
                logicalThreadCount,
                config.alpha,
                config.dt,
                config.dx(),
                config.dy(),
                config.boundaryTemperature
            );

        const auto endTime =
            std::chrono::steady_clock::now();

        const double elapsedSeconds =
            std::chrono::duration<double>(
                endTime - startTime
            ).count();

        const double finalCenterTemperature =
            current(
                centerRow,
                centerColumn
            );

        std::cout
            << "\n--- Results ---\n";

        std::cout
            << "Initial center temperature: "
            << initialCenterTemperature
            << " C\n";

        std::cout
            << "Final center temperature: "
            << finalCenterTemperature
            << " C\n";

        std::cout
            << "Executed quanta: "
            << stats.executedQuanta
            << '\n';

        std::cout
            << "Expected quanta: "
            << expectedQuanta(config)
            << '\n';

        std::cout
            << "Execution time: "
            << elapsedSeconds
            << " s\n";

        if (
            stats.executedQuanta !=
            expectedQuanta(config)
        ) {
            throw std::runtime_error(
                "Fine-Grained executed an unexpected number of quanta."
            );
        }

        std::cout
            << "Simulation completed successfully.\n";

        return 0;

    } catch (const std::exception& error) {
        std::cerr
            << "\nFine-Grained simulation failed.\n"
            << "Reason: "
            << error.what()
            << '\n';

        return 1;
    }
}
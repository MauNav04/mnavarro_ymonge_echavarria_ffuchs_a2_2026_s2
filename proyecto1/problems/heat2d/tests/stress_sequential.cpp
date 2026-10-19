#include "config.hpp"
#include "grid.hpp"
#include "heat2d.hpp"
#include "initial_conditions.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <utility>

namespace {

constexpr double PI = 3.14159265358979323846;

/**
 * @brief Almacena las métricas de error obtenidas al comparar contra la solución analítica.
 *
 * l2Rms representa el error cuadrático medio sobre toda la malla.
 * lInfinity representa el mayor error absoluto encontrado en cualquier celda.
 */
struct AnalyticalError {
    double l2Rms = 0.0;
    double lInfinity = 0.0;
};

/**
 * @brief Comprueba que el tiempo realmente simulado coincida con el tiempo solicitado.
 *
 * Esta validación detecta errores de redondeo o truncamiento al convertir
 * totalTime / dt en una cantidad entera de pasos temporales.
 */
void validateSimulatedTime(
    double requestedTime,
    double simulatedTime
) {
    const double tolerance = 100.0 * std::numeric_limits<double>::epsilon() * std::max(1.0, std::abs(requestedTime));

    if (std::abs(requestedTime - simulatedTime) > tolerance) {
        throw std::runtime_error(
            "Stress test failed: simulated time does not match requested time."
        );
    }
}

/**
 * @brief Comprueba que todas las temperaturas calculadas sean valores finitos.
 *
 * Detecta NaN, infinito positivo o infinito negativo, situaciones que normalmente
 * indicarían inestabilidad numérica, divisiones incorrectas o corrupción del cálculo.
 */
void validateFiniteTemperatures(
    const heat2d::Grid& grid
) {
    for (std::size_t row = 0; row < grid.rows(); ++row) {
        for (std::size_t col = 0; col < grid.cols(); ++col) {
            if (!std::isfinite(grid(row, col))) {
                throw std::runtime_error(
                    "Stress test failed: non-finite temperature detected."
                );
            }
        }
    }
}

/**
 * @brief Comprueba que los cuatro bordes mantengan exactamente la condición de Dirichlet esperada.
 */
void validateBoundaries(
    const heat2d::Grid& grid,
    double expectedTemperature
) {
    constexpr double tolerance = 1e-9;

    for (std::size_t col = 0; col < grid.cols(); ++col) {
        if (std::abs(grid(0, col) - expectedTemperature) > tolerance) {
            throw std::runtime_error("Stress test failed: top boundary changed.");
        }
        if (std::abs(grid(grid.rows() - 1, col) - expectedTemperature) > tolerance) {
            throw std::runtime_error("Stress test failed: bottom boundary changed.");
        }
    }

    for (std::size_t row = 0; row < grid.rows(); ++row) {
        if (std::abs(grid(row, 0) - expectedTemperature) > tolerance) {
            throw std::runtime_error("Stress test failed: left boundary changed.");
        }
        if (std::abs(grid(row, grid.cols() - 1) - expectedTemperature) > tolerance) {
            throw std::runtime_error("Stress test failed: right boundary changed.");
        }
    }
}

/**
 * @brief Comprueba que todas las temperaturas permanezcan dentro del rango físico esperado.
 *
 * Para la condición inicial positiva utilizada y bordes en cero, el principio máximo
 * de la ecuación del calor indica que no deberían aparecer temperaturas menores que
 * cero ni superiores al máximo inicial.
 */
void validateTemperatureRange(
    const heat2d::Grid& grid,
    double maximumTemperature
) {
    constexpr double tolerance = 1e-9;

    for (std::size_t row = 0; row < grid.rows(); ++row) {
        for (std::size_t col = 0; col < grid.cols(); ++col) {
            
            const double temperature = grid(row, col);

            if (temperature < -tolerance) {
                throw std::runtime_error("Stress test failed: negative temperature detected.");
            }
            if (temperature > maximumTemperature + tolerance) {
                throw std::runtime_error("Stress test failed: temperature exceeded initial maximum.");
            }
        }
    }
}

/**
 * @brief Comprueba la simetría espacial de la solución numérica.
 *
 * La distribución sinusoidal inicial y las condiciones de frontera son simétricas.
 * Por ello, la solución debe conservar simetría respecto al centro de la superficie.
 */
void validateSpatialSymmetry(
    const heat2d::Grid& grid
) {
    constexpr double tolerance = 1e-9;
    double maximumSymmetryError = 0.0;

    for (std::size_t row = 0; row < grid.rows(); ++row) {
        const std::size_t symmetricRow = grid.rows() - 1 - row;

        for (std::size_t col = 0; col < grid.cols(); ++col) {
            const std::size_t symmetricCol = grid.cols() - 1 - col;
            
            const double difference = std::abs(
                grid(row, col) - grid(symmetricRow, symmetricCol)
            );

            maximumSymmetryError = std::max(maximumSymmetryError, difference);
        }
    }

    if (maximumSymmetryError > tolerance) {
        throw std::runtime_error(
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
 */
double calculateTotalTemperature(
    const heat2d::Grid& grid
) {
    double totalTemperature = 0.0;

    for (std::size_t row = 0; row < grid.rows(); ++row) {
        for (std::size_t col = 0; col < grid.cols(); ++col) {
            totalTemperature += grid(row, col);
        }
    }

    return totalTemperature;
}

/**
 * @brief Comprueba que la energía térmica discreta disminuya entre dos pasos consecutivos.
 */
void validateEnergyDecrease(
    double previousEnergy,
    double currentEnergy,
    const heat2d::Grid& grid,
    double maximumTemperature
) {
    const double tolerance = 100.0 * std::numeric_limits<double>::epsilon() * 
                             static_cast<double>(grid.size()) * std::max(1.0, maximumTemperature);

    if (currentEnergy >= previousEnergy - tolerance) {
        throw std::runtime_error(
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
 * T(x,y,t) = Tmax * sin(pi*x/Lx) * sin(pi*y/Ly) * exp[-alpha*pi^2*(1/Lx^2 + 1/Ly^2)*t]
 */
double analyticalTemperature(
    double x,
    double y,
    double time,
    const heat2d::SimulationConfig& config
) {
    const double spatialX = std::sin(PI * x / config.lengthX);
    const double spatialY = std::sin(PI * y / config.lengthY);
    
    const double decayExponent = -config.alpha * PI * PI *
        (1.0 / (config.lengthX * config.lengthX) + 1.0 / (config.lengthY * config.lengthY)) * time;

    return config.maxTemperature * spatialX * spatialY * std::exp(decayExponent);
}

/**
 * @brief Calcula los errores L2 RMS y L infinito contra la solución analítica.
 */
AnalyticalError calculateAnalyticalError(
    const heat2d::Grid& grid,
    const heat2d::SimulationConfig& config,
    double simulatedTime
) {
    double squaredErrorSum = 0.0;
    double maximumError = 0.0;

    for (std::size_t row = 0; row < grid.rows(); ++row) {
        const double y = static_cast<double>(row) * config.dy();

        for (std::size_t col = 0; col < grid.cols(); ++col) {
            const double x = static_cast<double>(col) * config.dx();

            const double exactTemperature = analyticalTemperature(x, y, simulatedTime, config);
            const double numericalTemperature = grid(row, col);
            
            const double error = numericalTemperature - exactTemperature;

            squaredErrorSum += error * error;
            maximumError = std::max(maximumError, std::abs(error));
        }
    }

    AnalyticalError result;
    result.l2Rms = std::sqrt(squaredErrorSum / static_cast<double>(grid.size()));
    result.lInfinity = maximumError;

    return result;
}

/**
 * @brief Comprueba que el resultado numérico se mantenga suficientemente cerca de la solución exacta.
 */
void validateAnalyticalError(
    const AnalyticalError& error
) {
    constexpr double maximumL2RmsError = 5e-4;
    constexpr double maximumLInfinityError = 1e-3;

    if (error.l2Rms > maximumL2RmsError) {
        throw std::runtime_error("Stress test failed: L2 RMS analytical error exceeded tolerance.");
    }

    if (error.lInfinity > maximumLInfinityError) {
        throw std::runtime_error("Stress test failed: L-infinity analytical error exceeded tolerance.");
    }
}

}  // namespace

/**
 * @brief Ejecuta una prueba robusta y pesada sobre la implementación secuencial Heat2D.
 */
int main() {
    try {
        heat2d::SimulationConfig config;
        
        config.lengthX = 1.0;
        config.lengthY = 1.0;
        config.rows = 801;
        config.cols = 801;
        config.alpha = 0.05;
        config.dt = 0.000005;
        config.totalTime = 0.005;
        config.maxTemperature = 100.0;
        config.boundaryTemperature = 0.0;

        std::cout << "\n=== Heat2D Sequential Robust Stress Test ===\n";
        std::cout << std::fixed << std::setprecision(9);
        std::cout << "Grid: " << config.rows << " x " << config.cols << "\n";
        std::cout << "dx: " << config.dx() << " m\n";
        std::cout << "dy: " << config.dy() << " m\n";
        std::cout << "dt: " << config.dt << " s\n";
        std::cout << "Steps: " << config.steps() << "\n";

        if (!heat2d::isStable(config.alpha, config.dt, config.dx(), config.dy())) {
            throw std::runtime_error("Stress test configuration is numerically unstable.");
        }

        std::cout << "Stability: STABLE\n";

        heat2d::Grid current(config.rows, config.cols, config.boundaryTemperature);
        heat2d::Grid next(config.rows, config.cols, config.boundaryTemperature);

        heat2d::initializeSinusoidal(current, config.maxTemperature, config.lengthX, config.lengthY);
        heat2d::applyFixedBoundary(current, config.boundaryTemperature);

        validateFiniteTemperatures(current);
        validateBoundaries(current, config.boundaryTemperature);
        validateSpatialSymmetry(current);

        const std::size_t centerRow = config.rows / 2;
        const std::size_t centerCol = config.cols / 2;
        
        const double initialCenterTemperature = current(centerRow, centerCol);
        const double initialEnergy = calculateTotalTemperature(current);
        
        double previousEnergy = initialEnergy;
        double accumulatedDiffusionSeconds = 0.0;

        for (std::size_t step = 0; step < config.steps(); ++step) {
            
            const auto stepStart = std::chrono::steady_clock::now();
            
            heat2d::diffusionStep(
                current, next, config.alpha, config.dt, config.dx(), config.dy(), config.boundaryTemperature
            );

            const auto stepEnd = std::chrono::steady_clock::now();
            accumulatedDiffusionSeconds += std::chrono::duration<double>(stepEnd - stepStart).count();

            const double nextEnergy = calculateTotalTemperature(next);

            validateEnergyDecrease(previousEnergy, nextEnergy, next, config.maxTemperature);

            std::swap(current, next);
            previousEnergy = nextEnergy;
        }

        const double simulatedTime = static_cast<double>(config.steps()) * config.dt;
        validateSimulatedTime(config.totalTime, simulatedTime);

        const double finalCenterTemperature = current(centerRow, centerCol);
        const double finalEnergy = calculateTotalTemperature(current);

        validateFiniteTemperatures(current);
        validateBoundaries(current, config.boundaryTemperature);
        validateTemperatureRange(current, config.maxTemperature);
        validateSpatialSymmetry(current);

        if (finalCenterTemperature >= initialCenterTemperature) {
            throw std::runtime_error("Stress test failed: center temperature did not decrease.");
        }

        const AnalyticalError analyticalError = calculateAnalyticalError(current, config, simulatedTime);
        validateAnalyticalError(analyticalError);

        const std::size_t interiorCells = (config.rows - 2) * (config.cols - 2);
        const double totalCellUpdates = static_cast<double>(interiorCells) * static_cast<double>(config.steps());
        const double updatesPerSecond = totalCellUpdates / accumulatedDiffusionSeconds;
        const double energyReductionPercentage = 100.0 * (initialEnergy - finalEnergy) / initialEnergy;

        std::cout << "\n--- Numerical Results ---\n";
        std::cout << "Initial center temperature: " << initialCenterTemperature << " C\n";
        std::cout << "Final center temperature: " << finalCenterTemperature << " C\n";
        std::cout << "Initial thermal sum: " << initialEnergy << "\n";
        std::cout << "Final thermal sum: " << finalEnergy << "\n";
        std::cout << "Thermal sum reduction: " << energyReductionPercentage << " %\n";
        std::cout << "L2 RMS analytical error: " << analyticalError.l2Rms << " C\n";
        std::cout << "L-infinity analytical error: " << analyticalError.lInfinity << " C\n";

        std::cout << "\n--- Performance Information ---\n";
        std::cout << "Diffusion kernel time: " << accumulatedDiffusionSeconds << " s\n";
        std::cout << "Approximate cell updates: " << totalCellUpdates << "\n";
        std::cout << "Approximate updates per second: " << updatesPerSecond << "\n";

        std::cout << "\nValidations:\n";
        std::cout << "  [PASS] Numerical stability\n";
        std::cout << "  [PASS] Finite temperatures\n";
        std::cout << "  [PASS] Dirichlet boundaries\n";
        std::cout << "  [PASS] Physical temperature range\n";
        std::cout << "  [PASS] Spatial symmetry\n";
        std::cout << "  [PASS] Thermal energy decreases every step\n";
        std::cout << "  [PASS] Analytical L2 error\n";
        std::cout << "  [PASS] Analytical L-infinity error\n";
        std::cout << "  [PASS] Requested simulation time\n";

        std::cout << "\nStress test: PASSED\n\n";

        return 0;

    } catch (const std::exception& error) {
        std::cerr << "\nStress test: FAILED\n";
        std::cerr << "Reason: " << error.what() << "\n";
        return 1;
    }
}
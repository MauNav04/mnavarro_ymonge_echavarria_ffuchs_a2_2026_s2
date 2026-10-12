#pragma once

#include "fine_grained/logical_thread.hpp"

#include <cstddef>
#include <optional>
#include <vector>

namespace heat2d::fine_grained {

/**
 * @brief Planifica hilos lógicos utilizando una política Round-Robin.
 *
 * Cada llamada a nextThread() representa una nueva decisión de scheduling.
 * Después de cada quantum, el siguiente hilo lógico recibe el turno,
 * independientemente de si ocurrió o no un stall.
 */
class RoundRobinScheduler {
public:
    /**
     * @brief Crea un scheduler para una cantidad fija de hilos lógicos.
     *
     * @param logicalThreadCount Cantidad de hilos lógicos administrados.
     *
     * @throws std::invalid_argument si logicalThreadCount es cero.
     */
    explicit RoundRobinScheduler(
        std::size_t logicalThreadCount
    );
    /**
 * @brief Permite copiar el estado actual del scheduler.
 *
 * La copia conserva la posición Round-Robin actual, pero continúa
 * evolucionando de manera independiente.
 */
RoundRobinScheduler(const RoundRobinScheduler&) = default;

RoundRobinScheduler& operator=(
    const RoundRobinScheduler&
) = default;

/**
 * @brief Permite transferir el estado actual del scheduler.
 */
RoundRobinScheduler(
    RoundRobinScheduler&&
) noexcept = default;

RoundRobinScheduler& operator=(
    RoundRobinScheduler&&
) noexcept = default;

~RoundRobinScheduler() = default;

    /**
     * @brief Obtiene el índice del próximo hilo con trabajo pendiente.
     *
     * Los hilos que ya finalizaron son omitidos manteniendo el orden
     * circular entre los demás contextos activos.
     *
     * @param logicalThreads Hilos lógicos administrados por el scheduler.
     *
     * @return Índice del siguiente hilo o std::nullopt si todos terminaron.
     *
     * @throws std::invalid_argument si la cantidad de hilos recibida
     *         no coincide con la configuración del scheduler.
     */
    [[nodiscard]]
    std::optional<std::size_t> nextThread(
        const std::vector<LogicalThread>& logicalThreads
    );

    /**
     * @brief Reinicia el Round-Robin desde el primer hilo lógico.
     */
    void reset() noexcept;

    /**
     * @brief Retorna la cantidad de hilos configurados.
     */
    [[nodiscard]]
    std::size_t logicalThreadCount() const noexcept;

private:
    std::size_t logicalThreadCount_;
    std::size_t nextCandidate_;
};

}  // namespace heat2d::fine_grained
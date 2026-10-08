#pragma once

#include <cstddef>

namespace heat2d::fine_grained {

/**
 * @brief Identifica una celda dentro de la malla Heat2D.
 */
struct CellPosition {
    std::size_t row;
    std::size_t column;
};

/**
 * @brief Representa el contexto de ejecución de un hilo lógico simulado.
 *
 * LogicalThread no representa un hilo del sistema operativo.
 * Conserva el progreso de una región mientras un scheduler externo
 * decide cuándo puede consumir su siguiente quantum.
 *
 * La clase utiliza semántica de valor: copiar un LogicalThread crea
 * un snapshot independiente que conserva el progreso alcanzado.
 */
class LogicalThread {
public:
    /**
     * @brief Crea un hilo lógico asociado a una región rectangular.
     *
     * Los rangos utilizan la convención [inicio, fin).
     *
     * @throws std::invalid_argument si alguno de los rangos está invertido.
     * @throws std::overflow_error si la cantidad de celdas no cabe en std::size_t.
     */
    LogicalThread(
        std::size_t id,
        std::size_t firstRow,
        std::size_t lastRowExclusive,
        std::size_t firstColumn,
        std::size_t lastColumnExclusive
    );

    /**
     * @brief Permite copiar snapshots independientes del contexto.
     */
    LogicalThread(const LogicalThread&) = default;

    LogicalThread& operator=(const LogicalThread&) = default;

    /**
     * @brief Permite transferir el contexto de ejecución.
     */
    LogicalThread(LogicalThread&&) noexcept = default;

    LogicalThread& operator=(LogicalThread&&) noexcept = default;

    ~LogicalThread() = default;

    [[nodiscard]]
    std::size_t id() const noexcept;

    [[nodiscard]]
    bool hasWork() const noexcept;

    /**
     * @throws std::logic_error si el contexto ya terminó.
     */
    [[nodiscard]]
    CellPosition currentCell() const;

    /**
     * @brief Consume exactamente un quantum.
     *
     * Un quantum corresponde actualmente a una celda.
     *
     * @throws std::logic_error si no existe trabajo pendiente.
     */
    void advance();

    /**
     * @brief Reinicia el contexto para un nuevo timestep.
     */
    void reset() noexcept;

    [[nodiscard]]
    std::size_t assignedCells() const noexcept;

    [[nodiscard]]
    std::size_t completedQuanta() const noexcept;

private:
    /**
     * @brief Calcula de forma segura el tamaño de la región asignada.
     *
     * @throws std::overflow_error si la multiplicación excede std::size_t.
     */
    [[nodiscard]]
    std::size_t calculateAssignedCells() const;

    std::size_t id_;

    std::size_t firstRow_;
    std::size_t lastRowExclusive_;

    std::size_t firstColumn_;
    std::size_t lastColumnExclusive_;

    std::size_t currentRow_;
    std::size_t currentColumn_;

    std::size_t assignedCells_;
    std::size_t completedQuanta_;
};

}  // namespace heat2d::fine_grained
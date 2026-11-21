#pragma once

#include "fine_grained/logical_thread.hpp"

#include <cstddef>
#include <optional>
#include <vector>

namespace heat2d::fine_grained {

/**
 * @brief Schedules logical threads using a Round-Robin policy.
 *
 * Each call to nextThread() represents a new scheduling decision.
 * After each quantum, the next logical thread gets its turn,
 * regardless of whether a stall occurred or not.
 */
class RoundRobinScheduler {
public:
    /**
     * @brief Creates a scheduler for a fixed number of logical threads.
     *
     * @param logicalThreadCount Number of managed logical threads.
     *
     * @throws std::invalid_argument if logicalThreadCount is zero.
     */
    explicit RoundRobinScheduler(
        std::size_t logicalThreadCount
    );
    /**
 * @brief Allows copying the current state of the scheduler.
 *
 * The copy preserves the current Round-Robin position but continues
 * to evolve independently.
 */
RoundRobinScheduler(const RoundRobinScheduler&) = default;

RoundRobinScheduler& operator=(
    const RoundRobinScheduler&
) = default;

/**
 * @brief Allows transferring the current state of the scheduler.
 */
RoundRobinScheduler(
    RoundRobinScheduler&&
) noexcept = default;

RoundRobinScheduler& operator=(
    RoundRobinScheduler&&
) noexcept = default;

~RoundRobinScheduler() = default;

    /**
     * @brief Gets the index of the next thread with pending work.
     *
     * Threads that have already finished are skipped while maintaining
     * circular order among the other active contexts.
     *
     * @param logicalThreads Logical threads managed by the scheduler.
     *
     * @return Index of the next thread or std::nullopt if all have finished.
     *
     * @throws std::invalid_argument if the number of threads received
     *         does not match the scheduler configuration.
     */
    [[nodiscard]]
    std::optional<std::size_t> nextThread(
        const std::vector<LogicalThread>& logicalThreads
    );

    /**
     * @brief Resets the Round-Robin starting from the first logical thread.
     */
    void reset() noexcept;

    /**
     * @brief Returns the number of configured threads.
     */
    [[nodiscard]]
    std::size_t logicalThreadCount() const noexcept;

private:
    std::size_t logicalThreadCount_;
    std::size_t nextCandidate_;
};

}  // namespace heat2d::fine_grained
#include "fine_grained/round_robin_scheduler.hpp"

#include <stdexcept>

namespace heat2d::fine_grained {

/**
 * @brief Constructs a Round-Robin scheduler for a fixed number of logical contexts.
 *
 * @param logicalThreadCount The total number of simulated threads to manage.
 * @throws std::invalid_argument If initialized with 0 threads, as a scheduler 
 *         requires at least one execution context to function.
 */
RoundRobinScheduler::RoundRobinScheduler(
    std::size_t logicalThreadCount
)
    : logicalThreadCount_(logicalThreadCount),
      nextCandidate_(0) {

    if (logicalThreadCount_ == 0) {
        throw std::invalid_argument(
            "RoundRobinScheduler requires at least one logical thread."
        );
    }
}

/**
 * @brief Selects the next available logical thread with pending work.
 *
 * Simulates a preemptive OS scheduler with a strict Round-Robin policy. 
 * It polls threads sequentially in a circular queue. If a thread has exhausted 
 * its workload for the current timestep, the scheduler skips it without 
 * advancing the global simulated time.
 *
 * @param logicalThreads The collection of logical threads to poll.
 * @return std::optional<std::size_t> The index of the selected thread, or 
 *         std::nullopt if all threads have completed their work.
 * @throws std::invalid_argument If the provided vector size mismatches the 
 *         scheduler's immutable configuration.
 */
std::optional<std::size_t> RoundRobinScheduler::nextThread(
    const std::vector<LogicalThread>& logicalThreads
) {
    // Fast-fail to guarantee bounds-safety during polling.
    if (logicalThreads.size() != logicalThreadCount_) {
        throw std::invalid_argument(
            "Logical thread count does not match scheduler configuration."
        );
    }

    std::size_t candidate = nextCandidate_;

    // Circular polling loop. Bounded to exactly `logicalThreadCount_` iterations 
    // to prevent infinite loops when all threads are depleted (O(N) worst-case).
    for (
        std::size_t checkedThreads = 0;
        checkedThreads < logicalThreadCount_;
        ++checkedThreads
    ) {
        if (logicalThreads[candidate].hasWork()) {
            // Update the program counter for the next dispatch to ensure fairness.
            nextCandidate_ =
                (candidate + 1) % logicalThreadCount_;

            return candidate;
        }

        // Context is idle/finished. Move to the next sibling thread.
        candidate =
            (candidate + 1) % logicalThreadCount_;
    }

    // System idle state: No threads have pending quanta.
    return std::nullopt;
}

/**
 * @brief Re-arms the scheduler for a new temporal iteration.
 *
 * Forces the scheduler to restart dispatching from thread 0. This guarantees 
 * strict deterministic execution across multiple timesteps.
 */
void RoundRobinScheduler::reset() noexcept {
    nextCandidate_ = 0;
}

/**
 * @brief Retrieves the fixed number of contexts managed by this scheduler.
 */
std::size_t RoundRobinScheduler::logicalThreadCount() const noexcept {
    return logicalThreadCount_;
}

}  // namespace heat2d::fine_grained
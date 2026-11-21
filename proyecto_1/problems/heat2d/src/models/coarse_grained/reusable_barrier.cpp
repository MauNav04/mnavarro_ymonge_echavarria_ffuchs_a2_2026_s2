#include "coarse_grained/reusable_barrier.hpp"

#include <stdexcept>

namespace heat2d {

/**
 * @brief Constructs a generation-based cyclic barrier.
 *
 * @param participantCount The exact number of threads that must arrive at the 
 *        barrier before any are allowed to proceed.
 * 
 * @throws std::invalid_argument If participantCount is 0, as a barrier 
 *         cannot meaningfully synchronize zero threads and would deadlock 
 *         or trivially bypass synchronization logic.
 */
ReusableBarrier::ReusableBarrier(
    std::size_t participantCount
)
    : participantCount_(participantCount) {

    if (participantCount_ == 0) {
        throw std::invalid_argument(
            "ReusableBarrier requires at least one participant."
        );
    }
}

/**
 * @brief Blocks the calling thread until all participants have arrived.
 *
 * This implementation acts as a Reusable (Cyclic) Barrier. It relies on a 
 * `generation_` counter to distinguish between different synchronization phases 
 * (e.g., Timestep K vs Timestep K+1). This prevents a fast thread from escaping 
 * the barrier, finishing its next chunk of work, and wrapping around to re-enter 
 * the barrier before slower threads have even woken up from the previous phase.
 */
void ReusableBarrier::wait() {
    // Acquire the lock to safely evaluate and modify barrier state.
    std::unique_lock<std::mutex> lock(mutex_);

    // Capture the current generation before going to sleep. 
    // This is crucial to protect against spurious wakeups and phase bleeding.
    const std::size_t currentGeneration =
        generation_;

    ++waitingCount_;

    // Check if this is the last thread to arrive at the barrier.
    if (waitingCount_ == participantCount_) {
        // Arm the barrier for the next usage cycle.
        waitingCount_ = 0;
        
        // Advance the generation, signaling that the current phase is complete.
        ++generation_;

        // Wake up all sleeping threads.
        condition_.notify_all();
        return;
    }

    // Not the last thread. Put the thread to sleep.
    // The lambda predicate ensures the thread only wakes up if the generation 
    // has actually advanced, entirely eliminating spurious wakeup bugs.
    condition_.wait(
        lock,
        [this, currentGeneration]() {
            return generation_ != currentGeneration;
        }
    );
}

/**
 * @brief Retrieves the total number of threads required to trip the barrier.
 */
std::size_t
ReusableBarrier::participantCount() const noexcept {
    return participantCount_;
}

} // namespace heat2d
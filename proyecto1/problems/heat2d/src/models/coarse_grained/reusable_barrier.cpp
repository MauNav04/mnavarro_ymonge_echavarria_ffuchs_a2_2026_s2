#include "coarse_grained/reusable_barrier.hpp"

#include <stdexcept>

namespace heat2d {

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

void ReusableBarrier::wait() {
    std::unique_lock<std::mutex> lock(mutex_);

    const std::size_t currentGeneration =
        generation_;

    ++waitingCount_;

    if (waitingCount_ == participantCount_) {
        waitingCount_ = 0;
        ++generation_;

        condition_.notify_all();
        return;
    }

    condition_.wait(
        lock,
        [this, currentGeneration]() {
            return generation_ != currentGeneration;
        }
    );
}

std::size_t
ReusableBarrier::participantCount() const noexcept {
    return participantCount_;
}

} // namespace heat2d
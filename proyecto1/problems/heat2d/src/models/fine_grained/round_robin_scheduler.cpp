#include "fine_grained/round_robin_scheduler.hpp"

#include <stdexcept>

namespace heat2d::fine_grained {

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

std::optional<std::size_t> RoundRobinScheduler::nextThread(
    const std::vector<LogicalThread>& logicalThreads
) {
    if (logicalThreads.size() != logicalThreadCount_) {
        throw std::invalid_argument(
            "Logical thread count does not match scheduler configuration."
        );
    }

    std::size_t candidate = nextCandidate_;

    for (
        std::size_t checkedThreads = 0;
        checkedThreads < logicalThreadCount_;
        ++checkedThreads
    ) {
        if (logicalThreads[candidate].hasWork()) {
            nextCandidate_ =
                (candidate + 1) % logicalThreadCount_;

            return candidate;
        }

        candidate =
            (candidate + 1) % logicalThreadCount_;
    }

    return std::nullopt;
}

void RoundRobinScheduler::reset() noexcept {
    nextCandidate_ = 0;
}

std::size_t RoundRobinScheduler::logicalThreadCount() const noexcept {
    return logicalThreadCount_;
}

}  // namespace heat2d::fine_grained
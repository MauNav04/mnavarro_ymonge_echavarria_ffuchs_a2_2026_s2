#pragma once

#include <condition_variable>
#include <cstddef>
#include <mutex>

namespace heat2d {

/**
 * @brief Reusable barrier compatible with C++17.
 *
 * All participating threads must reach wait() before
 * any of them can continue to the next phase.
 */
class ReusableBarrier {
public:
    explicit ReusableBarrier(std::size_t participantCount);

    ReusableBarrier(const ReusableBarrier&) = delete;
    ReusableBarrier& operator=(const ReusableBarrier&) = delete;
    ReusableBarrier(ReusableBarrier&&) = delete;
    ReusableBarrier& operator=(ReusableBarrier&&) = delete;

    void wait();

    [[nodiscard]]
    std::size_t participantCount() const noexcept;

private:
    const std::size_t participantCount_;

    std::size_t waitingCount_{0};
    std::size_t generation_{0};

    std::mutex mutex_;
    std::condition_variable condition_;
};

} // namespace heat2d
#pragma once

#include <condition_variable>
#include <cstddef>
#include <mutex>

namespace heat2d {

/**
 * @brief Barrera reutilizable compatible con C++17.
 *
 * Todos los threads participantes deben alcanzar wait() antes
 * de que cualquiera pueda continuar hacia la siguiente fase.
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
#include "coarse_grained/reusable_barrier.hpp"

#include <atomic>
#include <cstddef>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {

/**
 * @brief Verifies that a barrier rejects an invalid participant count.
 *
 * @throws std::runtime_error If a barrier with zero participants is accepted.
 */
void testRejectsZeroParticipants() {
    bool rejected = false;

    try {
        heat2d::ReusableBarrier barrier(0);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }

    if (!rejected) {
        throw std::runtime_error(
            "Barrier accepted zero participants."
        );
    }
}

/**
 * @brief Verifies the single-participant case and barrier reuse.
 *
 * A barrier with one participant must release immediately and remain reusable
 * across multiple generations.
 *
 * @throws std::runtime_error If the barrier reports an incorrect participant count.
 */
void testSingleParticipant() {
    heat2d::ReusableBarrier barrier(1);

    constexpr std::size_t repetitions = 100;

    for (
        std::size_t repetition = 0;
        repetition < repetitions;
        ++repetition
    ) {
        barrier.wait();
    }

    if (barrier.participantCount() != 1) {
        throw std::runtime_error(
            "Barrier reports an incorrect participant count."
        );
    }
}

/**
 * @brief Verifies strict synchronization and repeated barrier reuse.
 *
 * One worker is intentionally delayed until every other worker reaches the
 * point immediately before the barrier. The delayed worker then publishes the
 * current generation before entering the barrier.
 *
 * After leaving the barrier, every worker must observe that generation. This
 * ensures that no participant can continue until all workers have reached the
 * synchronization point.
 *
 * A second barrier separates consecutive rounds so validation from one
 * generation cannot overlap with the next.
 *
 * @throws std::runtime_error If synchronization or generation reuse fails.
 */
void testStrictSynchronizationAndReuse() {
    constexpr std::size_t threadCount = 4;
    constexpr std::size_t rounds = 100;
    constexpr std::size_t slowThreadId = 0;

    heat2d::ReusableBarrier barrier(
        threadCount
    );

    std::atomic<std::size_t> fastArrivals{0};
    std::atomic<std::size_t> publishedGeneration{0};
    std::atomic<bool> invariantViolation{false};

    std::vector<std::thread> workers;
    workers.reserve(threadCount);

    for (
        std::size_t threadId = 0;
        threadId < threadCount;
        ++threadId
    ) {
        workers.emplace_back(
            [&, threadId]() {

                for (
                    std::size_t round = 0;
                    round < rounds;
                    ++round
                ) {
                    if (threadId == slowThreadId) {
                        const std::size_t expectedFastArrivals =
                            (round + 1) * (threadCount - 1);

                        while (
                            fastArrivals.load(
                                std::memory_order_acquire
                            ) < expectedFastArrivals
                        ) {
                            std::this_thread::yield();
                        }

                        publishedGeneration.store(
                            round + 1,
                            std::memory_order_release
                        );

                    } else {
                        fastArrivals.fetch_add(
                            1,
                            std::memory_order_acq_rel
                        );
                    }

                    barrier.wait();

                    /*
                     * Any worker released from the barrier must observe the
                     * generation published by the delayed participant.
                     */
                    const std::size_t observedGeneration =
                        publishedGeneration.load(
                            std::memory_order_acquire
                        );

                    if (observedGeneration < round + 1) {
                        invariantViolation.store(
                            true,
                            std::memory_order_release
                        );
                    }

                    /*
                     * Separate consecutive generations so all workers finish
                     * validating the current round before the next one begins.
                     */
                    barrier.wait();
                }
            }
        );
    }

    for (std::thread& worker : workers) {
        worker.join();
    }

    if (
        invariantViolation.load(
            std::memory_order_acquire
        )
    ) {
        throw std::runtime_error(
            "Barrier allowed a thread to continue before all "
            "participants arrived."
        );
    }

    const std::size_t expectedFastArrivals =
        rounds * (threadCount - 1);

    if (
        fastArrivals.load(
            std::memory_order_acquire
        ) != expectedFastArrivals
    ) {
        throw std::runtime_error(
            "Unexpected number of fast-thread arrivals."
        );
    }

    if (
        publishedGeneration.load(
            std::memory_order_acquire
        ) != rounds
    ) {
        throw std::runtime_error(
            "Barrier did not complete all generations."
        );
    }
}

}  // namespace

int main() {
    try {
        testRejectsZeroParticipants();
        testSingleParticipant();
        testStrictSynchronizationAndReuse();

        std::cout
            << "ReusableBarrier tests: PASSED\n";

        return 0;

    } catch (const std::exception& error) {
        std::cerr
            << "ReusableBarrier tests: FAILED\n"
            << error.what()
            << '\n';

        return 1;
    }
}
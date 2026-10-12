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
 * @brief Verifica que una barrera no pueda crearse sin participantes.
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
 * @brief Verifica el caso mínimo de una barrera con un participante.
 *
 * Una barrera con un único participante debe permitir continuar
 * inmediatamente y debe poder reutilizarse múltiples veces.
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
 * @brief Verifica sincronización estricta y reutilización de la barrera.
 *
 * El thread 0 actúa como participante intencionalmente lento.
 *
 * Los demás threads anuncian que están listos y entran inmediatamente
 * a la barrera. El thread 0 no llega hasta comprobar que todos los
 * participantes rápidos alcanzaron el punto previo a wait().
 *
 * Antes de entrar, el thread lento publica el número de generación.
 * Después de salir de la barrera, todos los threads verifican que dicha
 * generación ya haya sido publicada.
 *
 * Si wait() estuviera vacío, los threads rápidos cruzarían antes de que
 * thread 0 publicara la generación y el test fallaría.
 */
void testStrictSynchronizationAndReuse() {
    constexpr std::size_t threadCount = 4;
    constexpr std::size_t rounds = 100;
    constexpr std::size_t slowThreadId = 0;

    heat2d::ReusableBarrier barrier(
        threadCount
    );

    /*
     * Cantidad acumulada de llegadas de los threads rápidos al punto
     * inmediatamente anterior a la barrera.
     */
    std::atomic<std::size_t> fastArrivals{0};

    /*
     * Última generación publicada por el thread lento.
     *
     * Para la ronda r, después de la barrera todos los participantes
     * deben observar al menos r + 1.
     */
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

                        /*
                         * Thread 0 espera intencionalmente hasta que
                         * los otros threads hayan alcanzado el punto
                         * previo a la barrera.
                         */
                        const std::size_t expectedFastArrivals =
                            (round + 1) * (threadCount - 1);

                        while (
                            fastArrivals.load(
                                std::memory_order_acquire
                            ) < expectedFastArrivals
                        ) {
                            std::this_thread::yield();
                        }

                        /*
                         * Publica una marca que solamente puede existir
                         * antes de que la barrera de esta ronda sea
                         * liberada correctamente.
                         */
                        publishedGeneration.store(
                            round + 1,
                            std::memory_order_release
                        );

                    } else {

                        /*
                         * Los threads rápidos anuncian que alcanzaron
                         * el punto inmediatamente anterior a wait().
                         */
                        fastArrivals.fetch_add(
                            1,
                            std::memory_order_acq_rel
                        );
                    }

                    /*
                     * Barrera que estamos probando.
                     */
                    barrier.wait();

                    /*
                     * INVARIANTE:
                     *
                     * Si un thread salió de la barrera, thread 0 ya
                     * tuvo que llegar y publicar esta generación.
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
                     * Segunda barrera para impedir que una generación
                     * comience mientras otros participantes todavía
                     * están validando la generación anterior.
                     *
                     * También verifica que la barrera sea reutilizable.
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

} // namespace

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
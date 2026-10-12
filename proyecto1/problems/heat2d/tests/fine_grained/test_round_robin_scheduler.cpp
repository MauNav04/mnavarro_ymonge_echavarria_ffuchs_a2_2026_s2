#include "fine_grained/logical_thread.hpp"
#include "fine_grained/round_robin_scheduler.hpp"

#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <type_traits>
#include <utility>

namespace {

using heat2d::fine_grained::LogicalThread;
using heat2d::fine_grained::RoundRobinScheduler;

static_assert(
    std::is_copy_constructible<RoundRobinScheduler>::value,
    "RoundRobinScheduler must be copy constructible."
);

static_assert(
    std::is_copy_assignable<RoundRobinScheduler>::value,
    "RoundRobinScheduler must be copy assignable."
);

static_assert(
    std::is_move_constructible<RoundRobinScheduler>::value,
    "RoundRobinScheduler must be move constructible."
);

static_assert(
    std::is_move_assignable<RoundRobinScheduler>::value,
    "RoundRobinScheduler must be move assignable."
);

static_assert(
    std::is_destructible<RoundRobinScheduler>::value,
    "RoundRobinScheduler must be destructible."
);

/**
 * @brief Interrumpe la prueba cuando una condición no se cumple.
 */
void require(
    bool condition,
    const char* message
) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

/**
 * @brief Verifica la secuencia Round-Robin básica.
 */
void testRoundRobinSequence() {
    std::vector<LogicalThread> logicalThreads;

    logicalThreads.emplace_back(0, 1, 2, 1, 3);
    logicalThreads.emplace_back(1, 2, 3, 1, 3);
    logicalThreads.emplace_back(2, 3, 4, 1, 3);

    RoundRobinScheduler scheduler(logicalThreads.size());

    const std::vector<std::size_t> expectedSequence = {
        0, 1, 2,
        0, 1, 2
    };

    for (const std::size_t expectedThread : expectedSequence) {
        const auto selectedThread = scheduler.nextThread(logicalThreads);

        require(
            selectedThread.has_value(),
            "Scheduler stopped before all work was completed."
        );

        require(
            selectedThread.value() == expectedThread,
            "Scheduler violated the expected Round-Robin sequence."
        );

        logicalThreads[selectedThread.value()].advance();
    }

    require(
        !scheduler.nextThread(logicalThreads).has_value(),
        "Scheduler should report completion after all threads finish."
    );
}

/**
 * @brief Verifica que los contextos finalizados sean omitidos.
 */
void testCompletedThreadsAreSkipped() {
    std::vector<LogicalThread> logicalThreads;

    logicalThreads.emplace_back(0, 1, 2, 1, 2);
    logicalThreads.emplace_back(1, 2, 3, 1, 3);
    logicalThreads.emplace_back(2, 3, 4, 1, 3);

    RoundRobinScheduler scheduler(logicalThreads.size());

    auto selectedThread = scheduler.nextThread(logicalThreads);
    require(selectedThread == 0, "Scheduler should start with thread 0.");
    logicalThreads[0].advance();

    selectedThread = scheduler.nextThread(logicalThreads);
    require(selectedThread == 1, "Scheduler should select thread 1.");
    logicalThreads[1].advance();

    selectedThread = scheduler.nextThread(logicalThreads);
    require(selectedThread == 2, "Scheduler should select thread 2.");
    logicalThreads[2].advance();

    // T0 ya terminó. El scheduler debe omitirlo.
    selectedThread = scheduler.nextThread(logicalThreads);
    require(selectedThread == 1, "Scheduler did not skip completed thread 0.");
    logicalThreads[1].advance();

    selectedThread = scheduler.nextThread(logicalThreads);
    require(selectedThread == 2, "Scheduler did not continue Round-Robin among active threads.");
}

/**
 * @brief Verifica que reset() reinicie la posición del scheduler.
 */
void testReset() {
    std::vector<LogicalThread> logicalThreads;

    logicalThreads.emplace_back(0, 1, 2, 1, 3);
    logicalThreads.emplace_back(1, 2, 3, 1, 3);

    RoundRobinScheduler scheduler(logicalThreads.size());

    auto selectedThread = scheduler.nextThread(logicalThreads);
    require(selectedThread == 0, "Scheduler should initially select thread 0.");
    logicalThreads[0].advance();

    selectedThread = scheduler.nextThread(logicalThreads);
    require(selectedThread == 1, "Scheduler should select thread 1 after thread 0.");
    logicalThreads[1].advance();

    scheduler.reset();

    selectedThread = scheduler.nextThread(logicalThreads);
    require(selectedThread == 0, "Scheduler reset should restart scheduling from thread 0.");
}

/**
 * @brief Verifica el comportamiento con un único hilo lógico.
 */
void testSingleLogicalThread() {
    std::vector<LogicalThread> logicalThreads;

    logicalThreads.emplace_back(0, 1, 2, 1, 3);

    RoundRobinScheduler scheduler(logicalThreads.size());

    for (std::size_t quantum = 0; quantum < 2; ++quantum) {
        const auto selectedThread = scheduler.nextThread(logicalThreads);

        require(
            selectedThread == 0,
            "Single-thread scheduler should always select thread 0."
        );

        logicalThreads[0].advance();
    }

    require(
        !scheduler.nextThread(logicalThreads).has_value(),
        "Single-thread scheduler should finish when its context finishes."
    );
}

/**
 * @brief Verifica que no se pueda crear un scheduler sin contextos.
 */
void testZeroThreadCountIsRejected() {
    bool exceptionDetected = false;

    try {
        // Inicialización anónima para evitar advertencias de variables no usadas
        RoundRobinScheduler(0);
    } catch (const std::invalid_argument&) {
        exceptionDetected = true;
    }

    require(
        exceptionDetected,
        "Scheduler should reject zero logical threads."
    );
}

/**
 * @brief Verifica que scheduler y colección utilicen el mismo tamaño.
 */
void testMismatchedThreadCountIsRejected() {
    RoundRobinScheduler scheduler(2);
    std::vector<LogicalThread> logicalThreads;
    logicalThreads.emplace_back(0, 1, 2, 1, 2);

    bool exceptionDetected = false;

    try {
        static_cast<void>(
            scheduler.nextThread(logicalThreads)
        );
    } catch (const std::invalid_argument&) {
        exceptionDetected = true;
    }

    require(
        exceptionDetected,
        "Scheduler should reject a mismatched logical thread collection."
    );
}

/**
 * @brief Verifica que los contextos inicialmente vacíos sean omitidos.
 */
void testInitiallyEmptyThreadsAreSkipped() {
    std::vector<LogicalThread> logicalThreads;

    logicalThreads.emplace_back(10, 1, 1, 1, 4); // Vacía
    logicalThreads.emplace_back(20, 2, 3, 1, 3); // Activa
    logicalThreads.emplace_back(30, 4, 4, 1, 4); // Vacía

    RoundRobinScheduler scheduler(logicalThreads.size());
    const auto selectedThread = scheduler.nextThread(logicalThreads);

    require(
        selectedThread.has_value(),
        "Scheduler should find an active logical thread."
    );

    require(
        selectedThread.value() == 1,
        "Scheduler should skip initially empty logical threads."
    );
}

/**
 * @brief Verifica que una colección completamente vacía finalice.
 */
void testAllThreadsInitiallyEmpty() {
    std::vector<LogicalThread> logicalThreads;

    logicalThreads.emplace_back(10, 1, 1, 1, 4);
    logicalThreads.emplace_back(20, 2, 2, 1, 4);
    logicalThreads.emplace_back(30, 3, 3, 1, 4);

    RoundRobinScheduler scheduler(logicalThreads.size());
    const auto selectedThread = scheduler.nextThread(logicalThreads);

    require(
        !selectedThread.has_value(),
        "Scheduler should report completion when all threads start empty."
    );
}

/**
 * @brief Verifica que el scheduler utilice índices posicionales.
 */
void testThreadIdIsIndependentFromVectorIndex() {
    std::vector<LogicalThread> logicalThreads;

    logicalThreads.emplace_back(42, 1, 2, 1, 2);
    logicalThreads.emplace_back(7, 2, 3, 1, 2);
    logicalThreads.emplace_back(100, 3, 4, 1, 2);

    RoundRobinScheduler scheduler(logicalThreads.size());

    const std::vector<std::size_t> expectedIndices = {0, 1, 2};
    const std::vector<std::size_t> expectedIds = {42, 7, 100};

    for (std::size_t position = 0; position < expectedIndices.size(); ++position) {
        const auto selectedThread = scheduler.nextThread(logicalThreads);

        require(selectedThread.has_value(), "Scheduler unexpectedly stopped.");

        const std::size_t selectedIndex = selectedThread.value();

        require(
            selectedIndex == expectedIndices[position],
            "Scheduler returned an incorrect vector index."
        );

        require(
            logicalThreads[selectedIndex].id() == expectedIds[position],
            "Logical thread ID does not match the expected context."
        );

        logicalThreads[selectedIndex].advance();
    }
}

/**
 * @brief Verifica que el scheduler permanezca agotado al completar.
 */
void testRepeatedCallsAfterCompletion() {
    std::vector<LogicalThread> logicalThreads;

    logicalThreads.emplace_back(10, 1, 2, 1, 2);
    logicalThreads.emplace_back(20, 2, 3, 1, 2);

    RoundRobinScheduler scheduler(logicalThreads.size());

    auto selectedThread = scheduler.nextThread(logicalThreads);
    require(selectedThread == 0, "Scheduler should initially select index 0.");
    logicalThreads[0].advance();

    selectedThread = scheduler.nextThread(logicalThreads);
    require(selectedThread == 1, "Scheduler should select index 1.");
    logicalThreads[1].advance();

    for (std::size_t attempt = 0; attempt < 3; ++attempt) {
        require(
            !scheduler.nextThread(logicalThreads).has_value(),
            "Exhausted scheduler should continue returning nullopt."
        );
    }
}

/**
 * @brief Verifica los observadores const del scheduler.
 */
void testConstObservers() {
    const RoundRobinScheduler scheduler(4);

    require(
        scheduler.logicalThreadCount() == 4,
        "Const scheduler returned an incorrect thread count."
    );
}

/**
 * @brief Verifica semántica de copia.
 */
void testCopySemantics() {
    std::vector<LogicalThread> logicalThreads;

    logicalThreads.emplace_back(10, 1, 2, 1, 3);
    logicalThreads.emplace_back(20, 2, 3, 1, 3);
    logicalThreads.emplace_back(30, 3, 4, 1, 3);

    RoundRobinScheduler original(logicalThreads.size());

    const auto firstSelection = original.nextThread(logicalThreads);
    require(firstSelection == 0, "Original scheduler should begin at index 0.");

    RoundRobinScheduler copy = original;

    const auto copySelection = copy.nextThread(logicalThreads);
    require(copySelection == 1, "Copied scheduler did not preserve Round-Robin position.");

    const auto originalSelection = original.nextThread(logicalThreads);
    require(originalSelection == 1, "Advancing the copy unexpectedly modified the original scheduler.");
}

/**
 * @brief Verifica semántica de movimiento.
 */
void testMoveSemantics() {
    std::vector<LogicalThread> logicalThreads;

    logicalThreads.emplace_back(10, 1, 2, 1, 3);
    logicalThreads.emplace_back(20, 2, 3, 1, 3);

    RoundRobinScheduler source(logicalThreads.size());

    const auto firstSelection = source.nextThread(logicalThreads);
    require(firstSelection == 0, "Source scheduler should initially select index 0.");

    RoundRobinScheduler moved = std::move(source);

    const auto nextSelection = moved.nextThread(logicalThreads);
    require(nextSelection == 1, "Moved scheduler did not preserve its Round-Robin position.");
}

}  // namespace

int main() {
    try {
        testRoundRobinSequence();
        testCompletedThreadsAreSkipped();
        testInitiallyEmptyThreadsAreSkipped();
        testAllThreadsInitiallyEmpty();
        testThreadIdIsIndependentFromVectorIndex();
        testRepeatedCallsAfterCompletion();
        testReset();
        testSingleLogicalThread();
        testConstObservers();
        testCopySemantics();
        testMoveSemantics();
        testZeroThreadCountIsRejected();
        testMismatchedThreadCountIsRejected();

        std::cout << "RoundRobinScheduler tests: PASSED\n";
        return 0;

    } catch (const std::exception& error) {
        std::cerr << "RoundRobinScheduler tests: FAILED\nReason: " << error.what() << '\n';
        return 1;
    }
}
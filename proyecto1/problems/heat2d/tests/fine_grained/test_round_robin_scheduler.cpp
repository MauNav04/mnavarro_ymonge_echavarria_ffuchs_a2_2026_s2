/**
 * @file round_robin_scheduler_test.cpp
 * @brief Unit test suite for the Fine-Grained Round-Robin Scheduler.
 *
 * This suite rigorously validates the deterministic dispatching logic of the 
 * Round-Robin scheduler. Correctness here guarantees that the Fine-Grained 
 * model distributes simulated execution time fairly among logical contexts, 
 * simulating a true preemptive OS scheduler.
 */

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

/*
 * Compile-time assertions for value semantics.
 * 
 * WHY: Schedulers are often passed by value into execution harnesses or 
 * stored in dynamically resized containers. Asserting copy/move semantics 
 * ensures that the scheduler's internal state (e.g., the `nextCandidate_` pointer) 
 * transfers correctly without causing race conditions or state corruption.
 */
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
 * @brief Halts the test gracefully if a logical assertion fails.
 * 
 * WHY: Throwing `std::runtime_error` instead of using `assert()` allows the 
 * test harness to catch the error, print a readable diagnostic message, and 
 * exit with a standardized error code rather than invoking `std::abort()`.
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
 * @brief Validates the strict, cyclic polling order of the scheduler.
 * 
 * WHY: Fair scheduling is the core requirement of the Fine-Grained model. 
 * This test guarantees that the scheduler iterates through threads [0, 1, 2] 
 * and properly wraps around back to 0, ensuring no thread is starved.
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
 * @brief Ensures the scheduler bypasses exhausted contexts.
 * 
 * WHY: In uneven domain decompositions, some threads will finish their 
 * assigned row chunks before others. The scheduler must seamlessly skip 
 * finished threads (e.g., T0) without wasting cycles or attempting to 
 * execute them, which would trigger out-of-bounds errors.
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

    // T0 is now depleted. The scheduler must skip it.
    selectedThread = scheduler.nextThread(logicalThreads);
    require(selectedThread == 1, "Scheduler did not skip completed thread 0.");
    logicalThreads[1].advance();

    selectedThread = scheduler.nextThread(logicalThreads);
    require(selectedThread == 2, "Scheduler did not continue Round-Robin among active threads.");
}

/**
 * @brief Verifies that `reset()` correctly rewinds the scheduling logic.
 * 
 * WHY: The framework simulates thousands of sequential timesteps. The scheduler 
 * must be cleanly rewound to its initial state at the start of each global 
 * timestep to ensure the simulation remains perfectly deterministic.
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
 * @brief Validates edge-case behavior with only a single thread.
 * 
 * WHY: A 1-thread Fine-Grained execution should logically map to a purely 
 * sequential execution. The scheduler must handle this degenerate cyclic case 
 * without math errors (e.g., modulo 1).
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
 * @brief Asserts strict rejection of meaningless configurations.
 * 
 * WHY: A scheduler with 0 threads cannot perform any work and would likely 
 * cause a divide-by-zero error in the polling logic (`% threadCount`).
 */
void testZeroThreadCountIsRejected() {
    bool exceptionDetected = false;

    try {
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
 * @brief Prevents out-of-bounds memory accesses during polling.
 * 
 * WHY: The scheduler assumes the provided `vector` matches its internal fixed 
 * `logicalThreadCount_`. If passed a smaller vector, it would cause a segfault. 
 * If passed a larger vector, it would ignore threads. This enforces strict parity.
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
 * @brief Validates proper handling of empty topological regions.
 * 
 * WHY: When physical rows < logical threads, some threads are instantiated with 
 * zero assigned rows. The scheduler must transparently skip these empty threads 
 * without stalling.
 */
void testInitiallyEmptyThreadsAreSkipped() {
    std::vector<LogicalThread> logicalThreads;

    logicalThreads.emplace_back(10, 1, 1, 1, 4); // Empty
    logicalThreads.emplace_back(20, 2, 3, 1, 3); // Active
    logicalThreads.emplace_back(30, 4, 4, 1, 4); // Empty

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
 * @brief Ensures the scheduler cleanly halts if no work exists.
 * 
 * WHY: A zero-timestep simulation, or a grid with no interior area, results 
 * in all threads being empty. The scheduler must not infinitely loop searching 
 * for active threads.
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
 * @brief Verifies that the scheduler returns vector indices, not thread IDs.
 * 
 * WHY: The framework relies on vector indices (0 to N) to directly index 
 * arrays, rather than the arbitrary mathematical IDs assigned to the threads.
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
 * @brief Asserts the scheduler remains safely in a depleted state.
 * 
 * WHY: Calling `nextThread` after the simulation timestep is technically 
 * complete should not crash or restart the sequence; it should consistently 
 * return `std::nullopt` until explicitly reset.
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
 * @brief Validates `const` correctness of state observers.
 */
void testConstObservers() {
    const RoundRobinScheduler scheduler(4);

    require(
        scheduler.logicalThreadCount() == 4,
        "Const scheduler returned an incorrect thread count."
    );
}

/**
 * @brief Ensures copy construction perfectly replicates the scheduling phase.
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
 * @brief Ensures `std::move` preserves the internal polling pointer.
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
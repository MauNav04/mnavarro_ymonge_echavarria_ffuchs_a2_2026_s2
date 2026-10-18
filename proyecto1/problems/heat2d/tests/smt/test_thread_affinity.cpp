#include "smt/cpu_topology.hpp"
#include "smt/thread_affinity.hpp"

#include <cstdlib>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

/**
 * @brief Enforces a test condition.
 *
 * @param condition Condition that must evaluate to true.
 * @param message Error message reported if the condition fails.
 *
 * @throws std::runtime_error If the condition is false.
 */
void require(
    bool condition,
    const char* message
) {
    if (!condition) {
        throw std::runtime_error(
            message
        );
    }
}


/**
 * @brief Restores a thread affinity mask when leaving the current scope.
 *
 * The guard provides a best-effort safety net for tests that temporarily
 * modify the calling thread's affinity. Tests still perform explicit
 * restoration when restoration itself must be validated.
 */
class AffinityRestoreGuard {
public:
    /**
     * @brief Creates a guard for a previously captured affinity mask.
     *
     * @param originalAffinity Affinity mask to restore on destruction.
     */
    explicit AffinityRestoreGuard(
        std::vector<int> originalAffinity
    )
        : originalAffinity_(
            std::move(
                originalAffinity
            )
        )
    {
    }

    AffinityRestoreGuard(
        const AffinityRestoreGuard&
    ) = delete;

    AffinityRestoreGuard& operator=(
        const AffinityRestoreGuard&
    ) = delete;

    /**
     * @brief Attempts to restore the original affinity without propagating errors.
     */
    ~AffinityRestoreGuard()
    {
        try {
            heat2d::smt::setCurrentThreadAffinity(
                originalAffinity_
            );
        } catch (...) {
            /*
             * Destructors must not propagate exceptions. Explicit restoration
             * inside the tests remains responsible for detecting failures on
             * the normal execution path.
             */
        }
    }

private:
    std::vector<int> originalAffinity_;
};


/**
 * @brief Verifies pinning to one process-visible logical CPU.
 *
 * The test selects the first CPU from the physical-core-first placement order,
 * pins the calling thread exclusively to it, validates the resulting affinity
 * mask, and confirms that the original affinity can be restored.
 *
 * @param topology Detected CPU topology used to select a valid test CPU.
 *
 * @throws std::runtime_error If pinning or affinity restoration fails.
 */
void testPinningToAllowedCpu(
    const heat2d::smt::CpuTopology& topology
) {
    using namespace heat2d::smt;

    const std::vector<int> originalAffinity =
        getCurrentThreadAffinity();

    AffinityRestoreGuard restoreGuard(
        originalAffinity
    );

    const std::vector<int> physicalOrder =
        buildPhysicalCpuOrder(
            topology
        );

    require(
        !physicalOrder.empty(),
        "Physical CPU order cannot be empty."
    );

    const int targetCpu =
        physicalOrder.front();

    require(
        currentThreadCanRunOnCpu(
            targetCpu
        ),
        "Selected test CPU is not available in the original affinity mask."
    );

    pinCurrentThreadToCpu(
        targetCpu
    );

    require(
        currentThreadIsPinnedToCpu(
            targetCpu
        ),
        "Current thread was not pinned exclusively to the requested CPU."
    );

    const std::vector<int> pinnedAffinity =
        getCurrentThreadAffinity();

    require(
        pinnedAffinity.size() == 1,
        "Pinned affinity mask must contain exactly one logical CPU."
    );

    require(
        pinnedAffinity.front() ==
            targetCpu,
        "Pinned affinity mask contains the wrong logical CPU."
    );

    /*
     * Restore explicitly so restoration is validated as part of the test,
     * rather than relying only on the scope guard.
     */
    setCurrentThreadAffinity(
        originalAffinity
    );

    const std::vector<int> restoredAffinity =
        getCurrentThreadAffinity();

    require(
        restoredAffinity ==
            originalAffinity,
        "Original thread affinity mask was not restored correctly."
    );

    std::cout
        << "Pin/restore test CPU: CPU"
        << targetCpu
        << '\n';
}


/**
 * @brief Verifies affinity to multiple logical CPUs.
 *
 * When at least two physical cores are available, the test restricts the
 * calling thread to two logical CPUs and verifies that both remain available
 * without the thread being reported as exclusively pinned to either one.
 *
 * @param topology Detected CPU topology used to select the test CPUs.
 *
 * @throws std::runtime_error If the requested affinity mask is not applied
 *         or restored correctly.
 */
void testMultipleCpuAffinity(
    const heat2d::smt::CpuTopology& topology
) {
    using namespace heat2d::smt;

    const std::vector<int> originalAffinity =
        getCurrentThreadAffinity();

    AffinityRestoreGuard restoreGuard(
        originalAffinity
    );

    const std::vector<int> physicalOrder =
        buildPhysicalCpuOrder(
            topology
        );

    /*
     * A multi-CPU affinity mask cannot be exercised when only one physical
     * core is available to the process.
     */
    if (physicalOrder.size() < 2) {
        std::cout
            << "Multi-CPU affinity test: SKIPPED "
            << "(only one physical core available)\n";

        return;
    }

    const std::vector<int> requestedAffinity{
        physicalOrder[0],
        physicalOrder[1]
    };

    setCurrentThreadAffinity(
        requestedAffinity
    );

    const std::vector<int> actualAffinity =
        getCurrentThreadAffinity();

    require(
        actualAffinity ==
            requestedAffinity,
        "Multi-CPU affinity mask does not match the requested mask."
    );

    require(
        currentThreadCanRunOnCpu(
            requestedAffinity[0]
        ),
        "First requested CPU is missing from the affinity mask."
    );

    require(
        currentThreadCanRunOnCpu(
            requestedAffinity[1]
        ),
        "Second requested CPU is missing from the affinity mask."
    );

    require(
        !currentThreadIsPinnedToCpu(
            requestedAffinity[0]
        ),
        "A two-CPU affinity mask was incorrectly reported as pinned."
    );

    setCurrentThreadAffinity(
        originalAffinity
    );

    require(
        getCurrentThreadAffinity() ==
            originalAffinity,
        "Original affinity was not restored after multi-CPU test."
    );

    std::cout
        << "Multi-CPU affinity test: CPU"
        << requestedAffinity[0]
        << ", CPU"
        << requestedAffinity[1]
        << '\n';
}


/**
 * @brief Verifies rejection of invalid affinity specifications.
 *
 * The affinity API must reject negative CPU identifiers, empty affinity
 * masks, and duplicate logical CPU identifiers.
 *
 * @throws std::runtime_error If any invalid input is accepted.
 */
void testInvalidCpuIdentifiers()
{
    using namespace heat2d::smt;

    bool negativeRejected =
        false;

    try {
        pinCurrentThreadToCpu(
            -1
        );
    } catch (
        const std::invalid_argument&
    ) {
        negativeRejected =
            true;
    }

    require(
        negativeRejected,
        "Negative logical CPU ID was not rejected."
    );

    bool emptyMaskRejected =
        false;

    try {
        setCurrentThreadAffinity(
            {}
        );
    } catch (
        const std::invalid_argument&
    ) {
        emptyMaskRejected =
            true;
    }

    require(
        emptyMaskRejected,
        "Empty affinity mask was not rejected."
    );

    bool duplicateRejected =
        false;

    try {
        setCurrentThreadAffinity(
            {
                0,
                0
            }
        );
    } catch (
        const std::invalid_argument&
    ) {
        duplicateRejected =
            true;
    }

    require(
        duplicateRejected,
        "Duplicate CPU IDs were not rejected."
    );
}


/**
 * @brief Prints a labeled thread affinity mask.
 *
 * @param label Text displayed before the CPU identifiers.
 * @param affinity Logical CPU IDs contained in the affinity mask.
 */
void printAffinity(
    const char* label,
    const std::vector<int>& affinity
) {
    std::cout
        << label;

    for (
        std::size_t index = 0;
        index < affinity.size();
        ++index
    ) {
        if (index != 0) {
            std::cout
                << ", ";
        }

        std::cout
            << affinity[index];
    }

    std::cout
        << '\n';
}

}  // namespace


/**
 * @brief Runs the thread-affinity integration test suite.
 *
 * The suite validates exclusive CPU pinning, multi-CPU affinity masks,
 * invalid-input rejection, and restoration of the calling thread's original
 * affinity after all tests complete.
 *
 * @return EXIT_SUCCESS if every test passes; EXIT_FAILURE otherwise.
 */
int main()
{
    try {
        using namespace heat2d::smt;

        const CpuTopology topology =
            detectCpuTopology();

        const std::vector<int> initialAffinity =
            getCurrentThreadAffinity();

        std::cout
            << "\n=== Heat2D Thread Affinity Test ===\n\n";

        printAffinity(
            "Initial thread affinity: ",
            initialAffinity
        );

        std::cout
            << "Detected physical cores: "
            << physicalCoreCount(
                topology
            )
            << '\n';

        std::cout
            << "Detected logical CPUs:   "
            << logicalCpuCount(
                topology
            )
            << "\n\n";

        testPinningToAllowedCpu(
            topology
        );

        testMultipleCpuAffinity(
            topology
        );

        testInvalidCpuIdentifiers();

        /*
         * The complete suite must leave the caller with the same affinity
         * mask that was active before testing began.
         */
        require(
            getCurrentThreadAffinity() ==
                initialAffinity,
            "Test suite changed the caller's final affinity mask."
        );

        std::cout
            << "\nThread affinity tests: PASSED\n";

        return EXIT_SUCCESS;

    } catch (
        const std::exception& error
    ) {
        std::cerr
            << "\nThread affinity tests: FAILED\n"
            << "Reason: "
            << error.what()
            << '\n';

        return EXIT_FAILURE;
    }
}
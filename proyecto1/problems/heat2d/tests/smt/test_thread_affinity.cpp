#include "smt/cpu_topology.hpp"
#include "smt/thread_affinity.hpp"

#include <cstdlib>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

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
 * @brief Restores the original affinity even when the test fails.
 */
class AffinityRestoreGuard {
public:
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

    ~AffinityRestoreGuard()
    {
        try {
            heat2d::smt::setCurrentThreadAffinity(
                originalAffinity_
            );
        } catch (...) {
            /*
             * Destructors must not propagate exceptions.
             *
             * The explicit restore performed by the test will still
             * detect failures during the normal execution path.
             */
        }
    }

private:
    std::vector<int> originalAffinity_;
};


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
     * Restore the original mask explicitly so that restoration itself
     * also becomes part of the test.
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
     * On a single-CPU environment there is nothing meaningful to test
     * regarding a multi-CPU affinity mask.
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
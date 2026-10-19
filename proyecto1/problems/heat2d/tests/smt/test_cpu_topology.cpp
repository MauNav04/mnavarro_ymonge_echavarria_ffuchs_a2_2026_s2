#include "smt/cpu_topology.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <set>
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
 * @brief Prints a sequence of logical CPU identifiers.
 *
 * @param cpus Logical CPU IDs to print.
 */
void printCpuList(
    const std::vector<int>& cpus
) {
    for (
        std::size_t index = 0;
        index < cpus.size();
        ++index
    ) {
        if (index != 0) {
            std::cout << ", ";
        }

        std::cout
            << cpus[index];
    }
}


/**
 * @brief Validates the structural invariants of the detected CPU topology.
 *
 * The detected topology must contain at least one physical core and one
 * logical CPU. Every allowed logical CPU must belong to exactly one detected
 * physical core, and no core may reference a CPU outside the process affinity
 * mask.
 *
 * @param topology CPU topology detected for the current process.
 *
 * @throws std::runtime_error If any topology invariant is violated.
 */
void testDetectedTopology(
    const heat2d::smt::CpuTopology& topology
) {
    using namespace heat2d::smt;

    require(
        logicalCpuCount(topology) > 0,
        "Topology must contain at least one logical CPU."
    );

    require(
        physicalCoreCount(topology) > 0,
        "Topology must contain at least one physical core."
    );

    require(
        logicalCpuCount(topology) >=
            physicalCoreCount(topology),
        "Logical CPU count cannot be smaller than physical core count."
    );

    std::set<int> allowed(
        topology.allowedLogicalCpuIds.begin(),
        topology.allowedLogicalCpuIds.end()
    );

    require(
        allowed.size() ==
            topology.allowedLogicalCpuIds.size(),
        "Allowed CPU list contains duplicates."
    );

    std::set<int> discovered;

    for (
        const auto& core :
        topology.cores
    ) {
        require(
            !core.logicalCpuIds.empty(),
            "Detected physical core has no logical CPUs."
        );

        for (
            const int cpuId :
            core.logicalCpuIds
        ) {
            require(
                allowed.count(cpuId) == 1,
                "Core contains a CPU outside the process affinity mask."
            );

            require(
                discovered.insert(cpuId).second,
                "Logical CPU appears in more than one physical core."
            );
        }
    }

    require(
        discovered == allowed,
        "Detected cores do not cover every allowed logical CPU."
    );
}


/**
 * @brief Validates the physical-core-first CPU ordering.
 *
 * The physical ordering must select exactly one logical CPU from every
 * detected physical core, preserving the same core order stored in the
 * topology representation.
 *
 * @param topology CPU topology used to build the placement order.
 *
 * @throws std::runtime_error If the physical CPU order violates the expected
 *         topology mapping.
 */
void testPhysicalOrder(
    const heat2d::smt::CpuTopology& topology
) {
    using namespace heat2d::smt;

    const std::vector<int> physicalOrder =
        buildPhysicalCpuOrder(
            topology
        );

    require(
        physicalOrder.size() ==
            physicalCoreCount(topology),
        "Physical CPU order must contain exactly one CPU per core."
    );

    const std::set<int> uniqueCpus(
        physicalOrder.begin(),
        physicalOrder.end()
    );

    require(
        uniqueCpus.size() ==
            physicalOrder.size(),
        "Physical CPU order contains duplicate logical CPUs."
    );

    for (
        std::size_t coreIndex = 0;
        coreIndex < topology.cores.size();
        ++coreIndex
    ) {
        require(
            physicalOrder[coreIndex] ==
                topology.cores[coreIndex]
                    .logicalCpuIds
                    .front(),
            "Physical CPU ordering does not match core topology."
        );
    }
}


/**
 * @brief Validates the topology-aware SMT CPU ordering.
 *
 * The SMT order must contain every allowed logical CPU exactly once. Its
 * initial segment must first select one logical CPU from each physical core
 * before additional hardware threads from those cores are considered.
 *
 * @param topology CPU topology used to build the SMT placement order.
 *
 * @throws std::runtime_error If the SMT order violates the expected placement
 *         policy.
 */
void testSmtOrder(
    const heat2d::smt::CpuTopology& topology
) {
    using namespace heat2d::smt;

    const std::vector<int> physicalOrder =
        buildPhysicalCpuOrder(
            topology
        );

    const std::vector<int> smtOrder =
        buildSmtCpuOrder(
            topology
        );

    require(
        smtOrder.size() ==
            logicalCpuCount(topology),
        "SMT order must contain every allowed logical CPU."
    );

    const std::set<int> uniqueSmtCpus(
        smtOrder.begin(),
        smtOrder.end()
    );

    require(
        uniqueSmtCpus.size() ==
            smtOrder.size(),
        "SMT CPU order contains duplicate CPUs."
    );

    /*
     * The placement policy must use one logical CPU from every physical
     * core before assigning additional hardware threads from those cores.
     */
    require(
        smtOrder.size() >=
            physicalOrder.size(),
        "SMT CPU order is unexpectedly shorter than physical order."
    );

    require(
        std::equal(
            physicalOrder.begin(),
            physicalOrder.end(),
            smtOrder.begin()
        ),
        "SMT order must begin with one logical CPU per physical core."
    );
}


/**
 * @brief Prints the detected CPU topology and generated placement orders.
 *
 * The diagnostic output includes the process-visible logical CPUs, physical
 * core grouping, SMT characteristics, and the CPU orders used by the placement
 * helpers.
 *
 * @param topology CPU topology to print.
 */
void printTopology(
    const heat2d::smt::CpuTopology& topology
) {
    using namespace heat2d::smt;

    std::cout
        << "\n=== Heat2D Linux CPU Topology ===\n\n";

    std::cout
        << "Linux SMT control:       "
        << topology.linuxSmtControl
        << '\n';

    std::cout
        << "Allowed logical CPUs:    "
        << logicalCpuCount(topology)
        << '\n';

    std::cout
        << "Physical cores:          "
        << physicalCoreCount(topology)
        << '\n';

    std::cout
        << "Minimum threads/core:    "
        << minimumThreadsPerCore(topology)
        << '\n';

    std::cout
        << "Maximum threads/core:    "
        << maximumThreadsPerCore(topology)
        << '\n';

    std::cout
        << "Uniform threads/core:    "
        << (
            hasUniformThreadsPerCore(topology)
                ? "yes"
                : "no"
        )
        << '\n';

    std::cout
        << "SMT available:           "
        << (
            smtAvailable(topology)
                ? "yes"
                : "no"
        )
        << '\n';

    std::cout
        << "\nAllowed CPU IDs:\n  ";

    printCpuList(
        topology.allowedLogicalCpuIds
    );

    std::cout
        << "\n\nDetected physical cores:\n";

    for (
        std::size_t index = 0;
        index < topology.cores.size();
        ++index
    ) {
        const auto& core =
            topology.cores[index];

        std::cout
            << "  Core[" << index << "]"
            << " package=" << core.packageId
            << " core_id=" << core.coreId
            << " CPUs={";

        printCpuList(
            core.logicalCpuIds
        );

        std::cout
            << "}\n";
    }

    const std::vector<int> physicalOrder =
        buildPhysicalCpuOrder(
            topology
        );

    std::cout
        << "\nPhysical CPU order:\n  ";

    printCpuList(
        physicalOrder
    );

    const std::vector<int> smtOrder =
        buildSmtCpuOrder(
            topology
        );

    std::cout
        << "\n\nSMT CPU order:\n  ";

    printCpuList(
        smtOrder
    );

    std::cout
        << "\n\n";
}

}  // namespace


/**
 * @brief Runs the CPU-topology validation suite.
 *
 * Detects the process-visible Linux CPU topology, validates the public topology
 * representation, verifies both placement-order strategies, and prints the
 * resulting topology for diagnostic inspection.
 *
 * @return EXIT_SUCCESS if every validation passes; EXIT_FAILURE otherwise.
 */
int main()
{
    try {
        const heat2d::smt::CpuTopology topology =
            heat2d::smt::detectCpuTopology();

        /*
         * Detection performs its own validation, but invoking the public
         * validator explicitly also exercises its external API contract.
         */
        heat2d::smt::validateCpuTopology(
            topology
        );

        testDetectedTopology(
            topology
        );

        testPhysicalOrder(
            topology
        );

        testSmtOrder(
            topology
        );

        printTopology(
            topology
        );

        std::cout
            << "CPU topology tests: PASSED\n";

        return EXIT_SUCCESS;

    } catch (
        const std::exception& error
    ) {
        std::cerr
            << "\nCPU topology tests: FAILED\n"
            << "Reason: "
            << error.what()
            << '\n';

        return EXIT_FAILURE;
    }
}
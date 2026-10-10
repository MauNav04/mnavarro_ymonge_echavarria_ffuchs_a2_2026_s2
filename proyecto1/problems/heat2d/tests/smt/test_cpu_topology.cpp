#include "smt/cpu_topology.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <set>
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
     * Critical SMT property:
     *
     * Before using a second hardware thread from any physical core,
     * the placement must first use one CPU from every physical core.
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


int main()
{
    try {
        const heat2d::smt::CpuTopology topology =
            heat2d::smt::detectCpuTopology();

        /*
         * detectCpuTopology() already validates internally, but calling
         * the validator explicitly here also tests the public API.
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
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "smt/cpu_topology.hpp"

#include <sched.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace heat2d::smt {

namespace {

/**
 * @brief Reads a single integer value from a Linux sysfs file.
 * 
 * Used for extracting core/package IDs from the `/sys/devices/system/cpu/` virtual 
 * filesystem. Enforces strict parsing and bounds checking to ensure hardware data 
 * is correctly ingested without buffer overflows or invalid conversions.
 *
 * @throws std::runtime_error If the file is missing, unreadable, or contains non-integers.
 */
int readIntegerFile(
    const std::string& path
) {
    std::ifstream input(
        path
    );

    if (!input) {
        throw std::runtime_error(
            "Unable to open Linux CPU topology file: " +
            path
        );
    }

    long long value = 0;

    if (!(input >> value)) {
        throw std::runtime_error(
            "Unable to parse Linux CPU topology file: " +
            path
        );
    }

    if (
        value <
            static_cast<long long>(
                std::numeric_limits<int>::min()
            ) ||
        value >
            static_cast<long long>(
                std::numeric_limits<int>::max()
            )
    ) {
        throw std::runtime_error(
            "CPU topology value exceeds int range: " +
            path
        );
    }

    return static_cast<int>(
        value
    );
}


/**
 * @brief Probes the Linux kernel's global SMT control state.
 *
 * Attempts to read `/sys/devices/system/cpu/smt/control`. This information is 
 * purely diagnostic (e.g., to log if HyperThreading is forced off at the OS level) 
 * and does not directly alter the dynamic affinity logic.
 * 
 * @return std::string "on", "off", "forceoff", "notsupported", or "unavailable".
 */
std::string readLinuxSmtControl()
{
    const std::string path =
        "/sys/devices/system/cpu/smt/control";

    std::ifstream input(
        path
    );

    if (!input) {
        return "unavailable";
    }

    std::string state;

    if (!(input >> state)) {
        return "unavailable";
    }

    return state;
}


/**
 * @brief Resolves the exact subset of logical CPUs available to the current process.
 *
 * `sched_getaffinity()` is deliberately preferred over standard API calls like 
 * `sysconf(_SC_NPROCESSORS_ONLN)` or `std::thread::hardware_concurrency()`. 
 * This ensures the thread-pinning logic strictly respects external constraints 
 * like cgroups, Docker cpuset allocations, or `taskset` limitations.
 * 
 * @throws std::runtime_error If the syscall fails or returns an empty CPU set.
 */
std::vector<int> detectAllowedLogicalCpus()
{
    cpu_set_t cpuSet;

    CPU_ZERO(
        &cpuSet
    );

    if (
        sched_getaffinity(
            0,
            sizeof(cpuSet),
            &cpuSet
        ) != 0
    ) {
        const int errorNumber =
            errno;

        throw std::runtime_error(
            "sched_getaffinity() failed: " +
            std::string(
                std::strerror(
                    errorNumber
                )
            )
        );
    }

    std::vector<int> cpus;

    for (
        int cpuId = 0;
        cpuId < CPU_SETSIZE;
        ++cpuId
    ) {
        if (
            CPU_ISSET(
                cpuId,
                &cpuSet
            )
        ) {
            cpus.push_back(
                cpuId
            );
        }
    }

    if (cpus.empty()) {
        throw std::runtime_error(
            "The process affinity mask contains no logical CPUs."
        );
    }

    return cpus;
}


/**
 * @brief Constructs the absolute sysfs path for a specific logical CPU's topology.
 */
std::string cpuTopologyPath(
    int cpuId
) {
    return
        "/sys/devices/system/cpu/cpu" +
        std::to_string(
            cpuId
        ) +
        "/topology/";
}

}  // namespace


/**
 * @brief Discovers the physical and logical CPU mapping by scanning Linux sysfs.
 * 
 * Gathers the `physical_package_id` (socket) and `core_id` (physical core) for 
 * every logical CPU allowed by the process affinity mask. This creates an exact 
 * internal representation of the hardware, distinguishing between true physical 
 * separation and shared-resource sibling threads (SMT/HyperThreading).
 */
CpuTopology detectCpuTopology()
{
    CpuTopology topology;

    topology.allowedLogicalCpuIds =
        detectAllowedLogicalCpus();

    topology.linuxSmtControl =
        readLinuxSmtControl();

    /*
     * Linux core_id values are not guaranteed to be globally unique across sockets.
     *
     * Therefore, the true physical-core identity is defined by the composite key:
     *
     *   (physical_package_id, core_id)
     */
    using CoreKey =
        std::pair<int, int>;

    std::map<
        CoreKey,
        std::vector<int>
    > logicalCpusByCore;

    for (
        const int cpuId :
        topology.allowedLogicalCpuIds
    ) {
        const std::string topologyPath =
            cpuTopologyPath(
                cpuId
            );

        const int packageId =
            readIntegerFile(
                topologyPath +
                "physical_package_id"
            );

        const int coreId =
            readIntegerFile(
                topologyPath +
                "core_id"
            );

        logicalCpusByCore[
            {
                packageId,
                coreId
            }
        ].push_back(
            cpuId
        );
    }

    topology.cores.reserve(
        logicalCpusByCore.size()
    );

    /*
     * std::map provides deterministic ascending ordering by:
     *
     *   1. packageId (Socket)
     *   2. coreId (Physical Core)
     *
     * Sorting the mapped vectors guarantees logical CPUs inside every core 
     * are also deterministically ordered.
     */
    for (
        auto& entry :
        logicalCpusByCore
    ) {
        auto& logicalCpuIds =
            entry.second;

        std::sort(
            logicalCpuIds.begin(),
            logicalCpuIds.end()
        );

        CpuCore core;

        core.packageId =
            entry.first.first;

        core.coreId =
            entry.first.second;

        core.logicalCpuIds =
            std::move(
                logicalCpuIds
            );

        topology.cores.push_back(
            std::move(
                core
            )
        );
    }

    // Safety guard against malformed sysfs trees.
    validateCpuTopology(
        topology
    );

    return topology;
}


/**
 * @brief Asserts the integrity of a discovered CPU topology structure.
 * 
 * Verifies that no logical CPUs were lost or duplicated during parsing, 
 * every physical core contains at least one execution thread, and every 
 * physical core possesses a globally unique package/core identifier tuple.
 */
void validateCpuTopology(
    const CpuTopology& topology
) {
    if (
        topology.allowedLogicalCpuIds.empty()
    ) {
        throw std::runtime_error(
            "CPU topology does not contain any allowed logical CPUs."
        );
    }

    if (
        topology.cores.empty()
    ) {
        throw std::runtime_error(
            "CPU topology does not contain any physical cores."
        );
    }

    const std::set<int> allowedCpuSet(
        topology.allowedLogicalCpuIds.begin(),
        topology.allowedLogicalCpuIds.end()
    );

    if (
        allowedCpuSet.size() !=
        topology.allowedLogicalCpuIds.size()
    ) {
        throw std::runtime_error(
            "CPU topology contains duplicate allowed logical CPUs."
        );
    }

    std::set<int> discoveredCpuSet;

    for (
        const CpuCore& core :
        topology.cores
    ) {
        if (
            core.logicalCpuIds.empty()
        ) {
            throw std::runtime_error(
                "CPU topology contains a physical core "
                "without logical CPUs."
            );
        }

        for (
            const int cpuId :
            core.logicalCpuIds
        ) {
            if (
                allowedCpuSet.find(
                    cpuId
                ) ==
                allowedCpuSet.end()
            ) {
                throw std::runtime_error(
                    "CPU topology assigned a logical CPU "
                    "that is outside the process affinity mask."
                );
            }

            const auto insertion =
                discoveredCpuSet.insert(
                    cpuId
                );

            if (
                !insertion.second
            ) {
                throw std::runtime_error(
                    "A logical CPU belongs to more than "
                    "one detected physical core."
                );
            }
        }
    }

    if (
        discoveredCpuSet !=
        allowedCpuSet
    ) {
        throw std::runtime_error(
            "Not every allowed logical CPU was assigned "
            "to exactly one physical core."
        );
    }

    /*
     * Confirm that physical-core identities are unique.
     */
    std::set<
        std::pair<int, int>
    > physicalCoreKeys;

    for (
        const CpuCore& core :
        topology.cores
    ) {
        const auto insertion =
            physicalCoreKeys.insert(
                {
                    core.packageId,
                    core.coreId
                }
            );

        if (
            !insertion.second
        ) {
            throw std::runtime_error(
                "Duplicate physical-core identity detected."
            );
        }
    }
}


std::size_t physicalCoreCount(
    const CpuTopology& topology
) noexcept {
    return topology.cores.size();
}


std::size_t logicalCpuCount(
    const CpuTopology& topology
) noexcept {
    return topology.allowedLogicalCpuIds.size();
}


std::size_t minimumThreadsPerCore(
    const CpuTopology& topology
) noexcept {
    if (
        topology.cores.empty()
    ) {
        return 0;
    }

    std::size_t minimum =
        topology.cores.front()
            .logicalCpuIds
            .size();

    for (
        const CpuCore& core :
        topology.cores
    ) {
        minimum =
            std::min(
                minimum,
                core.logicalCpuIds.size()
            );
    }

    return minimum;
}


std::size_t maximumThreadsPerCore(
    const CpuTopology& topology
) noexcept {
    std::size_t maximum =
        0;

    for (
        const CpuCore& core :
        topology.cores
    ) {
        maximum =
            std::max(
                maximum,
                core.logicalCpuIds.size()
            );
    }

    return maximum;
}


bool hasUniformThreadsPerCore(
    const CpuTopology& topology
) noexcept {
    if (
        topology.cores.empty()
    ) {
        return true;
    }

    const std::size_t expected =
        topology.cores.front()
            .logicalCpuIds
            .size();

    for (
        const CpuCore& core :
        topology.cores
    ) {
        if (
            core.logicalCpuIds.size() !=
            expected
        ) {
            return false;
        }
    }

    return true;
}


bool smtAvailable(
    const CpuTopology& topology
) noexcept {
    return
        maximumThreadsPerCore(
            topology
        ) > 1;
}

/**
 * @brief Constructs an affinity ordering strictly using distinct physical cores.
 * 
 * Collects the first logical thread from each physical core. Guarantees true 
 * physical parallelism without ALU/cache resource contention, up to the 
 * total physical core count.
 */
std::vector<int> buildPhysicalCpuOrder(
    const CpuTopology& topology
) {
    validateCpuTopology(
        topology
    );

    std::vector<int> order;

    order.reserve(
        topology.cores.size()
    );

    /*
     * Select exactly one logical CPU from every physical core.
     */
    for (
        const CpuCore& core :
        topology.cores
    ) {
        order.push_back(
            core.logicalCpuIds.front()
        );
    }

    return order;
}

/**
 * @brief Constructs an optimal breadth-first SMT affinity ordering.
 * 
 * Spreads execution across distinct physical cores *before* assigning secondary 
 * sibling threads. 
 *
 * Pass 0: Grab the 1st logical CPU from every core.
 * Pass 1: Grab the 2nd logical CPU from every core (invoking HyperThreading).
 * ...
 * 
 * This strategy guarantees that worker counts up to `physical_cores` operate 
 * with dedicated L1 caches and ALUs, while larger thread pools seamlessly saturate 
 * SMT capabilities.
 */
std::vector<int> buildSmtCpuOrder(
    const CpuTopology& topology
) {
    validateCpuTopology(
        topology
    );

    std::vector<int> order;

    order.reserve(
        logicalCpuCount(
            topology
        )
    );

    const std::size_t maximumSiblingCount =
        maximumThreadsPerCore(
            topology
        );

    /*
     * Traverse sibling index first, then physical core.
     *
     * Pass 0:
     *   first logical CPU from every core.
     *
     * Pass 1:
     *   second logical CPU from every core.
     *
     * etc.
     *
     * This guarantees that worker counts up to the number of physical
     * cores use different physical cores before consuming SMT siblings.
     */
    for (
        std::size_t siblingIndex = 0;
        siblingIndex < maximumSiblingCount;
        ++siblingIndex
    ) {
        for (
            const CpuCore& core :
            topology.cores
        ) {
            if (
                siblingIndex <
                core.logicalCpuIds.size()
            ) {
                order.push_back(
                    core.logicalCpuIds[
                        siblingIndex
                    ]
                );
            }
        }
    }

    // Final safety check to ensure all detected CPUs were mapped.
    if (
        order.size() !=
        logicalCpuCount(
            topology
        )
    ) {
        throw std::runtime_error(
            "SMT CPU ordering did not contain every logical CPU."
        );
    }

    return order;
}

}  // namespace heat2d::smt
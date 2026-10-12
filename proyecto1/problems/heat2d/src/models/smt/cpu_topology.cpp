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
 * @brief Reads one integer value from a Linux sysfs file.
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
 * @brief Reads the Linux kernel SMT control state.
 *
 * This information is diagnostic only. Topology detection itself does
 * not depend on the presence of this file.
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
 * @brief Obtains the logical CPUs that the current process is allowed
 *        to execute on.
 *
 * sched_getaffinity() is deliberately preferred over sysconf() because
 * the system may contain CPUs that are unavailable to this process due
 * to cpusets, containers, taskset or scheduler restrictions.
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
 * @brief Returns the sysfs topology directory for one logical CPU.
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


CpuTopology detectCpuTopology()
{
    CpuTopology topology;

    topology.allowedLogicalCpuIds =
        detectAllowedLogicalCpus();

    topology.linuxSmtControl =
        readLinuxSmtControl();

    /*
     * Linux core_id values are not guaranteed to be globally unique.
     *
     * Therefore the physical-core identity is:
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
     * std::map provides deterministic ordering by:
     *
     *   packageId
     *   coreId
     *
     * Logical CPUs inside every core are also sorted.
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

    validateCpuTopology(
        topology
    );

    return topology;
}


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
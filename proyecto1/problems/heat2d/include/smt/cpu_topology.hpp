#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace heat2d::smt {

/**
 * @brief Represents one physical CPU core visible to the current process.
 *
 * packageId and coreId are obtained from Linux sysfs.
 *
 * logicalCpuIds contains only logical CPUs that are currently available
 * inside the process affinity mask.
 *
 * Example:
 *
 *   packageId = 0
 *   coreId = 3
 *   logicalCpuIds = {3, 11}
 *
 * would mean that logical CPUs 3 and 11 are SMT siblings belonging
 * to the same physical core.
 */
struct CpuCore {
    int packageId{0};
    int coreId{0};

    std::vector<int> logicalCpuIds;
};


/**
 * @brief Linux CPU topology visible to the current process.
 *
 * This structure intentionally describes the CPUs available to the
 * process rather than blindly assuming that every CPU existing in
 * /sys/devices/system/cpu can be used.
 */
struct CpuTopology {
    std::vector<int> allowedLogicalCpuIds;

    std::vector<CpuCore> cores;

    /*
     * Content of:
     *
     *   /sys/devices/system/cpu/smt/control
     *
     * Typical values include:
     *   on
     *   off
     *   forceoff
     *   notsupported
     *   notimplemented
     *   unavailable
     */
    std::string linuxSmtControl;
};


/**
 * @brief Detects the Linux CPU topology available to the current process.
 *
 * Detection strategy:
 *
 *   1. sched_getaffinity() determines which logical CPUs this process
 *      is actually allowed to execute on.
 *
 *   2. For each allowed CPU, Linux sysfs provides:
 *
 *      /sys/devices/system/cpu/cpuN/topology/core_id
 *      /sys/devices/system/cpu/cpuN/topology/physical_package_id
 *
 *   3. CPUs with the same package_id and core_id are grouped as
 *      hardware threads belonging to the same physical core.
 *
 * @throws std::runtime_error if the process CPU affinity mask cannot
 *         be obtained or if required topology information is invalid.
 */
[[nodiscard]]
CpuTopology detectCpuTopology();


/**
 * @brief Validates internal consistency of a detected topology.
 *
 * Checks:
 *   - at least one logical CPU exists,
 *   - at least one physical core exists,
 *   - no logical CPU appears more than once,
 *   - every core contains at least one logical CPU,
 *   - every CPU assigned to a core belongs to the process affinity mask.
 *
 * @throws std::runtime_error if the topology is inconsistent.
 */
void validateCpuTopology(
    const CpuTopology& topology
);


/**
 * @brief Returns the number of physical cores visible to the process.
 */
[[nodiscard]]
std::size_t physicalCoreCount(
    const CpuTopology& topology
) noexcept;


/**
 * @brief Returns the number of logical CPUs visible to the process.
 */
[[nodiscard]]
std::size_t logicalCpuCount(
    const CpuTopology& topology
) noexcept;


/**
 * @brief Returns the minimum number of available hardware threads
 *        observed in any physical core.
 */
[[nodiscard]]
std::size_t minimumThreadsPerCore(
    const CpuTopology& topology
) noexcept;


/**
 * @brief Returns the maximum number of available hardware threads
 *        observed in any physical core.
 */
[[nodiscard]]
std::size_t maximumThreadsPerCore(
    const CpuTopology& topology
) noexcept;


/**
 * @brief Returns true when every visible physical core exposes the
 *        same number of logical CPUs.
 */
[[nodiscard]]
bool hasUniformThreadsPerCore(
    const CpuTopology& topology
) noexcept;


/**
 * @brief Returns true when at least one physical core exposes more
 *        than one hardware thread to the current process.
 */
[[nodiscard]]
bool smtAvailable(
    const CpuTopology& topology
) noexcept;


/**
 * @brief Builds an ordering containing one logical CPU per physical core.
 *
 * This is the ordering used for the physical-core-only experiment.
 *
 * No CPU numbering convention is assumed.
 */
[[nodiscard]]
std::vector<int> buildPhysicalCpuOrder(
    const CpuTopology& topology
);


/**
 * @brief Builds a topology-aware SMT ordering.
 *
 * The first pass selects one logical CPU from every physical core.
 * The second pass selects the second sibling from every physical core,
 * and so on.
 *
 * For an 8C/16T processor the resulting logical structure is:
 *
 *   Core0.thread0
 *   Core1.thread0
 *   ...
 *   Core7.thread0
 *   Core0.thread1
 *   Core1.thread1
 *   ...
 *   Core7.thread1
 *
 * The actual Linux CPU IDs may be completely different.
 */
[[nodiscard]]
std::vector<int> buildSmtCpuOrder(
    const CpuTopology& topology
);

}  // namespace heat2d::smt
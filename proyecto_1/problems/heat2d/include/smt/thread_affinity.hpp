#pragma once

#include <vector>

namespace heat2d::smt {

/**
 * @brief Returns the logical CPUs currently allowed for the calling thread.
 *
 * The returned CPU IDs are sorted in ascending order.
 *
 * @throws std::runtime_error if pthread_getaffinity_np() fails.
 */
[[nodiscard]]
std::vector<int> getCurrentThreadAffinity();


/**
 * @brief Replaces the affinity mask of the calling thread.
 *
 * Every logical CPU ID must:
 *
 *   - be non-negative,
 *   - fit inside cpu_set_t,
 *   - appear only once.
 *
 * @param logicalCpuIds CPUs on which the current thread may execute.
 *
 * @throws std::invalid_argument if the requested CPU list is invalid.
 * @throws std::runtime_error if pthread_setaffinity_np() fails.
 */
void setCurrentThreadAffinity(
    const std::vector<int>& logicalCpuIds
);


/**
 * @brief Pins the calling thread to exactly one logical CPU.
 *
 * After this call succeeds, Linux may execute the thread only on the
 * requested logical CPU.
 *
 * @param logicalCpuId Linux logical CPU identifier.
 *
 * @throws std::invalid_argument if the CPU ID cannot be represented by
 *         cpu_set_t.
 * @throws std::runtime_error if pthread_setaffinity_np() fails.
 */
void pinCurrentThreadToCpu(
    int logicalCpuId
);


/**
 * @brief Returns true if the current thread is pinned exclusively to
 *        the requested logical CPU.
 */
[[nodiscard]]
bool currentThreadIsPinnedToCpu(
    int logicalCpuId
);


/**
 * @brief Returns true if one logical CPU belongs to the current
 *        thread's affinity mask.
 */
[[nodiscard]]
bool currentThreadCanRunOnCpu(
    int logicalCpuId
);

}  // namespace heat2d::smt
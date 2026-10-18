#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "smt/thread_affinity.hpp"

#include <pthread.h>
#include <sched.h>

#include <cerrno>
#include <cstring>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace heat2d::smt {

namespace {

/**
 * @brief Validates a Linux logical CPU ID against the system's `cpu_set_t` limits.
 *
 * Ensures that the ID can be safely manipulated by the POSIX thread affinity API 
 * without causing out-of-bounds memory accesses or silent truncation.
 *
 * @throws std::invalid_argument If the ID is negative or exceeds `CPU_SETSIZE`.
 */
void validateLogicalCpuId(
    int logicalCpuId
) {
    if (logicalCpuId < 0) {
        throw std::invalid_argument(
            "Logical CPU ID cannot be negative."
        );
    }

    if (logicalCpuId >= CPU_SETSIZE) {
        throw std::invalid_argument(
            "Logical CPU ID exceeds cpu_set_t capacity."
        );
    }
}


/**
 * @brief Converts a pthread error code into a descriptive C++ exception.
 *
 * POSIX thread functions (`pthread_*`) return their error numbers directly 
 * as integers, rather than setting the global `errno` variable. This helper 
 * safely translates those codes into readable `std::runtime_error` messages.
 */
std::runtime_error pthreadAffinityError(
    const char* operation,
    int errorNumber
) {
    return std::runtime_error(
        std::string(operation) +
        " failed: " +
        std::strerror(
            errorNumber
        )
    );
}


/**
 * @brief Constructs a POSIX `cpu_set_t` bitmask from a vector of CPU IDs.
 *
 * @throws std::invalid_argument If the input vector is empty, contains negative 
 *         values, out-of-bound IDs, or duplicate CPU assignments.
 */
cpu_set_t createCpuSet(
    const std::vector<int>& logicalCpuIds
) {
    if (logicalCpuIds.empty()) {
        throw std::invalid_argument(
            "Thread affinity mask cannot be empty."
        );
    }

    cpu_set_t cpuSet;

    // Clear the mask before setting specific bits.
    CPU_ZERO(
        &cpuSet
    );

    std::set<int> uniqueCpuIds;

    for (
        const int logicalCpuId :
        logicalCpuIds
    ) {
        validateLogicalCpuId(
            logicalCpuId
        );

        const auto insertion =
            uniqueCpuIds.insert(
                logicalCpuId
            );

        if (!insertion.second) {
            throw std::invalid_argument(
                "Thread affinity mask contains duplicate CPU IDs."
            );
        }

        // Enable the bit corresponding to the logical CPU.
        CPU_SET(
            logicalCpuId,
            &cpuSet
        );
    }

    return cpuSet;
}

}  // namespace

/**
 * @brief Retrieves the current thread's CPU affinity mask.
 * 
 * Interrogates the Linux kernel scheduler to determine which logical CPUs 
 * the calling thread is currently permitted to execute on.
 *
 * @throws std::runtime_error If the `pthread_getaffinity_np` syscall fails 
 *         or returns an inexplicably empty mask.
 */
std::vector<int> getCurrentThreadAffinity()
{
    cpu_set_t cpuSet;

    CPU_ZERO(
        &cpuSet
    );

    const int result =
        pthread_getaffinity_np(
            pthread_self(),
            sizeof(cpuSet),
            &cpuSet
        );

    if (result != 0) {
        throw pthreadAffinityError(
            "pthread_getaffinity_np()",
            result
        );
    }

    std::vector<int> logicalCpuIds;

    for (
        int logicalCpuId = 0;
        logicalCpuId < CPU_SETSIZE;
        ++logicalCpuId
    ) {
        if (
            CPU_ISSET(
                logicalCpuId,
                &cpuSet
            )
        ) {
            logicalCpuIds.push_back(
                logicalCpuId
            );
        }
    }

    if (logicalCpuIds.empty()) {
        throw std::runtime_error(
            "Current thread affinity mask is empty."
        );
    }

    return logicalCpuIds;
}

/**
 * @brief Restricts the calling thread's execution to a specific subset of CPUs.
 *
 * Instructs the Linux kernel scheduler to only migrate this thread between 
 * the provided logical CPU IDs. Used in NUMA architectures or when attempting 
 * to isolate workloads to prevent L1/L2 cache evictions.
 */
void setCurrentThreadAffinity(
    const std::vector<int>& logicalCpuIds
) {
    const cpu_set_t cpuSet =
        createCpuSet(
            logicalCpuIds
        );

    const int result =
        pthread_setaffinity_np(
            pthread_self(),
            sizeof(cpuSet),
            &cpuSet
        );

    if (result != 0) {
        throw pthreadAffinityError(
            "pthread_setaffinity_np()",
            result
        );
    }
}

/**
 * @brief Binds the calling thread exclusively to a single logical CPU.
 * 
 * This is the core "Thread Pinning" function utilized by the SMT model. By 
 * restricting a worker thread to exactly one logical CPU, it eliminates 
 * OS-induced context migrations, guaranteeing maximum spatial locality and 
 * cache retention.
 */
void pinCurrentThreadToCpu(
    int logicalCpuId
) {
    validateLogicalCpuId(
        logicalCpuId
    );

    setCurrentThreadAffinity(
        {
            logicalCpuId
        }
    );
}

/**
 * @brief Verifies if the calling thread is strictly pinned to the specified CPU.
 * 
 * Returns true *only* if the thread's affinity mask contains exactly one 
 * allowed CPU, and that CPU matches the provided `logicalCpuId`.
 */
bool currentThreadIsPinnedToCpu(
    int logicalCpuId
) {
    validateLogicalCpuId(
        logicalCpuId
    );

    const std::vector<int> affinity =
        getCurrentThreadAffinity();

    return
        affinity.size() == 1 &&
        affinity.front() ==
            logicalCpuId;
}

/**
 * @brief Checks if the thread is permitted to run on the specified CPU.
 * 
 * Returns true if the provided `logicalCpuId` is present within the thread's 
 * current affinity mask, regardless of how many other CPUs it is also allowed 
 * to execute on.
 */
bool currentThreadCanRunOnCpu(
    int logicalCpuId
) {
    validateLogicalCpuId(
        logicalCpuId
    );

    const std::vector<int> affinity =
        getCurrentThreadAffinity();

    for (
        const int allowedCpu :
        affinity
    ) {
        if (
            allowedCpu ==
            logicalCpuId
        ) {
            return true;
        }
    }

    return false;
}

}  // namespace heat2d::smt
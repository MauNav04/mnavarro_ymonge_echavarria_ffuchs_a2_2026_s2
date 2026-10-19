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
 * @brief Validates that a logical CPU ID can be represented by `cpu_set_t`.
 *
 * @param logicalCpuId Linux logical CPU identifier.
 *
 * @throws std::invalid_argument If the ID is negative or exceeds
 *         the capacity of `cpu_set_t`.
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
 * @brief Builds a descriptive exception from a pthread error code.
 *
 * POSIX pthread functions return error codes directly instead of relying
 * on the global `errno` value.
 *
 * @param operation Name of the pthread operation that failed.
 * @param errorNumber Error code returned by the pthread API.
 * @return Runtime error containing the operation and error description.
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
 * @brief Builds a CPU affinity mask from logical CPU identifiers.
 *
 * Each CPU ID is validated before being inserted into the mask.
 * Duplicate identifiers are rejected to keep the affinity specification
 * unambiguous.
 *
 * @param logicalCpuIds Logical CPUs allowed by the affinity mask.
 * @return Initialized `cpu_set_t` containing the requested CPUs.
 *
 * @throws std::invalid_argument If the input is empty, contains an invalid
 *         CPU ID, or contains duplicate IDs.
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

        CPU_SET(
            logicalCpuId,
            &cpuSet
        );
    }

    return cpuSet;
}

}  // namespace

/**
 * @brief Returns the logical CPUs available to the calling thread.
 *
 * Queries the current pthread affinity mask and converts the active CPU bits
 * into a vector of Linux logical CPU identifiers.
 *
 * @return Logical CPU IDs present in the current affinity mask.
 *
 * @throws std::runtime_error If the pthread affinity query fails or if the
 *         returned affinity mask is empty.
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
 * @brief Restricts the calling thread to a set of logical CPUs.
 *
 * The resulting affinity mask limits the CPUs on which the Linux scheduler
 * may execute the calling thread.
 *
 * @param logicalCpuIds Logical CPUs allowed for the calling thread.
 *
 * @throws std::invalid_argument If the requested affinity mask is invalid.
 * @throws std::runtime_error If the pthread affinity operation fails.
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
 * @brief Pins the calling thread to one logical CPU.
 *
 * Restricting a worker to a single logical CPU provides explicit and
 * deterministic CPU placement for the SMT execution model and prevents
 * the scheduler from migrating that worker to another logical CPU.
 *
 * @param logicalCpuId Logical CPU to which the thread will be pinned.
 *
 * @throws std::invalid_argument If the logical CPU ID is invalid.
 * @throws std::runtime_error If the affinity operation fails.
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
 * @brief Checks whether the calling thread is pinned exclusively to one CPU.
 *
 * @param logicalCpuId Logical CPU expected in the affinity mask.
 * @return true if the affinity mask contains only the requested CPU;
 *         false otherwise.
 *
 * @throws std::invalid_argument If the logical CPU ID is invalid.
 * @throws std::runtime_error If the current affinity mask cannot be obtained.
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
 * @brief Checks whether the calling thread may execute on a logical CPU.
 *
 * @param logicalCpuId Logical CPU to search for in the current affinity mask.
 * @return true if the CPU is present in the affinity mask; false otherwise.
 *
 * @throws std::invalid_argument If the logical CPU ID is invalid.
 * @throws std::runtime_error If the current affinity mask cannot be obtained.
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
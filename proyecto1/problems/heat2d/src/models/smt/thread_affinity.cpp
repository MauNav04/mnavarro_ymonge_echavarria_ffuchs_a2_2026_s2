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
 * @brief Ensures that one Linux logical CPU ID can be represented by
 *        the fixed-size cpu_set_t used by the pthread affinity API.
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
 * @brief Converts a pthread error number into a readable exception.
 *
 * pthread_* functions return their error number directly. They do not
 * necessarily report failures through errno.
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
 * @brief Builds a cpu_set_t from a validated vector of CPU IDs.
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
#include "SystemResources.h"

#include <psapi.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace {
constexpr PDH_STATUS kPdhMoreData = static_cast<PDH_STATUS>(0x800007D2L);

uint64_t fileTimeValue(const FILETIME& value) {
    ULARGE_INTEGER combined{};
    combined.LowPart = value.dwLowDateTime;
    combined.HighPart = value.dwHighDateTime;
    return combined.QuadPart;
}

double percentOf(uint64_t value, uint64_t total) {
    if (total == 0) return 0;
    return std::clamp(static_cast<double>(value) * 100.0 / static_cast<double>(total), 0.0, 100.0);
}
}

SystemResources::~SystemResources() {
    if (gpuSample_.valid()) gpuSample_.wait();
    if (gpuQuery_) PdhCloseQuery(gpuQuery_);
    closeJob();
}

bool SystemResources::initialize() {
    MEMORYSTATUSEX memory{sizeof(memory)};
    if (GlobalMemoryStatusEx(&memory)) totalPhysicalBytes_ = memory.ullTotalPhys;
    logicalProcessorCount_ = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    if (logicalProcessorCount_ == 0) logicalProcessorCount_ = 1;

    FILETIME creation{}, exit{}, kernel{}, user{};
    if (GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user))
        previousProcessTime_ = fileTimeValue(kernel) + fileTimeValue(user);
    QueryPerformanceCounter(&previousSampleTime_);

    if (PdhOpenQueryW(nullptr, 0, &gpuQuery_) == ERROR_SUCCESS &&
        PdhAddEnglishCounterW(gpuQuery_, L"\\GPU Engine(*)\\Utilization Percentage", 0, &gpuCounter_) == ERROR_SUCCESS) {
        PdhCollectQueryData(gpuQuery_);
    } else if (gpuQuery_) {
        PdhCloseQuery(gpuQuery_);
        gpuQuery_ = nullptr;
        gpuCounter_ = nullptr;
    }
    cached_.totalPhysicalBytes = totalPhysicalBytes_;
    return totalPhysicalBytes_ != 0;
}

SystemResourceSnapshot SystemResources::sample() {
    const ULONGLONG nowTick = GetTickCount64();
    if (lastSampleTick_ != 0 && nowTick - lastSampleTick_ < 500) return cached_;
    lastSampleTick_ = nowTick;

    FILETIME creation{}, exit{}, kernel{}, user{};
    LARGE_INTEGER nowCounter{};
    if (GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user) && QueryPerformanceCounter(&nowCounter)) {
        const uint64_t processTime = fileTimeValue(kernel) + fileTimeValue(user);
        LARGE_INTEGER frequency{};
        QueryPerformanceFrequency(&frequency);
        const LONGLONG elapsedCounter = nowCounter.QuadPart - previousSampleTime_.QuadPart;
        if (elapsedCounter > 0 && frequency.QuadPart > 0 && processTime >= previousProcessTime_) {
            const double elapsedSeconds = static_cast<double>(elapsedCounter) / static_cast<double>(frequency.QuadPart);
            const double processSeconds = static_cast<double>(processTime - previousProcessTime_) / 10'000'000.0;
            const double totalCpuPercent = processSeconds * 100.0 /
                (elapsedSeconds * static_cast<double>(logicalProcessorCount_));
            cached_.cpuPercent = std::clamp(static_cast<int>(std::lround(totalCpuPercent)), 0, 100);
        }
        previousProcessTime_ = processTime;
        previousSampleTime_ = nowCounter;
    }

    PROCESS_MEMORY_COUNTERS_EX memory{};
    memory.cb = sizeof(memory);
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory))) {
        cached_.workingSetBytes = memory.WorkingSetSize;
        cached_.memoryPercent = percentOf(memory.WorkingSetSize, totalPhysicalBytes_);
    }
    cached_.totalPhysicalBytes = totalPhysicalBytes_;
    if (gpuSample_.valid() && gpuSample_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        cached_.gpuPercent = gpuSample_.get();
        cached_.gpuAvailable = cached_.gpuPercent >= 0;
    }
    if (gpuQuery_ && !gpuSample_.valid()) {
        try {
            gpuSample_ = std::async(std::launch::async, [this] { return collectGpuPercent(); });
        } catch (const std::system_error&) {
            cached_.gpuAvailable = false;
        }
    }
    return cached_;
}

int SystemResources::collectGpuPercent() {
    if (gpuQuery_ && gpuCounter_ && PdhCollectQueryData(gpuQuery_) == ERROR_SUCCESS) {
        DWORD bytes = 0, count = 0;
        PDH_STATUS status = PdhGetFormattedCounterArrayW(gpuCounter_, PDH_FMT_DOUBLE, &bytes, &count, nullptr);
        if (status == kPdhMoreData && bytes > 0) {
            std::vector<unsigned char> storage(bytes);
            auto* entries = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(storage.data());
            status = PdhGetFormattedCounterArrayW(gpuCounter_, PDH_FMT_DOUBLE, &bytes, &count, entries);
            if (status == ERROR_SUCCESS) {
                const std::wstring processInstance = L"pid_" + std::to_wstring(GetCurrentProcessId()) + L"_";
                double total = 0;
                for (DWORD i = 0; i < count; ++i) {
                    if (!entries[i].szName || std::wstring(entries[i].szName).find(processInstance) == std::wstring::npos) continue;
                    if (entries[i].FmtValue.CStatus == ERROR_SUCCESS)
                        total += std::max(0.0, entries[i].FmtValue.doubleValue);
                }
                return std::clamp(static_cast<int>(std::lround(total)), 0, 100);
            }
        }
    }
    return -1;
}

bool SystemResources::setMemoryLimitPercent(int percent, std::wstring& error) {
    error.clear();
    percent = std::clamp(percent, 10, 100);
    if (totalPhysicalBytes_ == 0) {
        error = L"시스템 메모리 정보를 읽을 수 없습니다.";
        return false;
    }
    if (percent < 100) {
        PROCESS_MEMORY_COUNTERS_EX current{};
        current.cb = sizeof(current);
        const uint64_t budget = (totalPhysicalBytes_ / 100) * static_cast<uint64_t>(percent);
        if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&current), sizeof(current)) &&
            current.PrivateUsage > budget) {
            error = L"현재 사용 중인 메모리보다 낮은 상한은 적용할 수 없습니다.";
            return false;
        }
    }
    if (percent < 100 && !memoryJob_) {
        memoryJob_ = CreateJobObjectW(nullptr, nullptr);
        if (!memoryJob_) {
            error = L"메모리 사용 상한을 만들지 못했습니다.";
            return false;
        }
    }
    if (!memoryJob_) return true;

    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    if (percent < 100) {
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_PROCESS_MEMORY;
        limits.ProcessMemoryLimit = static_cast<SIZE_T>((totalPhysicalBytes_ / 100) * static_cast<uint64_t>(percent));
    }
    if (!SetInformationJobObject(memoryJob_, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
        error = L"메모리 사용 상한을 적용하지 못했습니다.";
        return false;
    }
    if (!assignedToJob_) {
        if (!AssignProcessToJobObject(memoryJob_, GetCurrentProcess())) {
            error = L"현재 실행 환경에서 메모리 사용 상한을 적용할 수 없습니다.";
            closeJob();
            return false;
        }
        assignedToJob_ = true;
    }
    return true;
}

int SystemResources::cpuThrottleWaitMilliseconds(int limitPercent, int targetFps) const {
    limitPercent = std::clamp(limitPercent, 10, 100);
    if (limitPercent >= 100) return 0;
    const int threshold = std::max(0, limitPercent - 2);
    if (cached_.cpuPercent <= threshold) return 0;
    const double progress = std::clamp((cached_.cpuPercent - threshold) / 2.0, 0.0, 1.0);
    const double interval = targetFps > 0 ? 1000.0 / std::clamp(targetFps, 1, 1000) : 4.0;
    return static_cast<int>(std::ceil(interval * 0.95 * progress * progress));
}

void SystemResources::closeJob() {
    if (memoryJob_) CloseHandle(memoryJob_);
    memoryJob_ = nullptr;
    assignedToJob_ = false;
}

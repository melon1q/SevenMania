#pragma once

#include <windows.h>
#include <pdh.h>
#include <cstdint>
#include <string>
#include <future>

struct SystemResourceSnapshot {
    int cpuPercent{};
    double memoryPercent{};
    int gpuPercent{};
    bool gpuAvailable{};
    uint64_t workingSetBytes{};
    uint64_t totalPhysicalBytes{};
};

class SystemResources {
public:
    SystemResources() = default;
    ~SystemResources();
    SystemResources(const SystemResources&) = delete;
    SystemResources& operator=(const SystemResources&) = delete;

    bool initialize();
    SystemResourceSnapshot sample();
    uint64_t totalPhysicalMemoryBytes() const { return totalPhysicalBytes_; }
    bool setMemoryLimitPercent(int percent, std::wstring& error);
    int cpuThrottleWaitMilliseconds(int limitPercent, int targetFps) const;

private:
    void closeJob();
    int collectGpuPercent();
    std::future<int> gpuSample_;

    PDH_HQUERY gpuQuery_{};
    PDH_HCOUNTER gpuCounter_{};
    HANDLE memoryJob_{};
    bool assignedToJob_{};
    uint64_t totalPhysicalBytes_{};
    uint64_t previousProcessTime_{};
    LARGE_INTEGER previousSampleTime_{};
    DWORD logicalProcessorCount_{1};
    SystemResourceSnapshot cached_{};
    ULONGLONG lastSampleTick_{};
};

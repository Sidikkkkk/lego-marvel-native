#pragma once
#include "logger.h"
#include <sys/sysinfo.h>
#include <cstdint>
#include <cinttypes>
#include <string>

enum class DeviceTier {
    LOW_END,
    STANDARD
};

struct DeviceProfile {
    uint64_t totalRamMB = 0;
    uint64_t freeRamMB = 0;
    DeviceTier tier = DeviceTier::LOW_END;
    uint64_t safeHeapLimitMB = 1152;
    size_t fsStreamBufferKB = 64;
    float renderScale = 0.55f;
};

inline DeviceProfile g_Device;

inline void detectDeviceProfile() {
    struct sysinfo info{};
    if (sysinfo(&info) == 0) {
        g_Device.totalRamMB = (static_cast<uint64_t>(info.totalram) * info.mem_unit) / (1024 * 1024);
        g_Device.freeRamMB  = (static_cast<uint64_t>(info.freeram)  * info.mem_unit) / (1024 * 1024);
    }

    if (g_Device.totalRamMB <= 4608) {
        g_Device.tier = DeviceTier::LOW_END;
        g_Device.safeHeapLimitMB = 1152;
        g_Device.fsStreamBufferKB = 64;
        g_Device.renderScale = 0.55f;
    } else {
        g_Device.tier = DeviceTier::STANDARD;
        g_Device.safeHeapLimitMB = 2048;
        g_Device.fsStreamBufferKB = 256;
        g_Device.renderScale = 0.75f;
    }

    writeLog("[DEVICE] Total RAM: %" PRIu64 " MB | Free RAM: %" PRIu64 " MB | Mode: %s",
             g_Device.totalRamMB,
             g_Device.freeRamMB,
             g_Device.tier == DeviceTier::LOW_END ? "LOW_END" : "STANDARD");
}

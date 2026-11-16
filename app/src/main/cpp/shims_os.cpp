#include "logger.h"
#include "device_profile.h"
#include <cstdint>
#include <cinttypes>
#include <algorithm>

extern "C" void shim_nn_init_Start() {
    writeLog("[OS] nn::init::Start() dipanggil");
}

extern "C" uint32_t shim_nn_os_SetMemoryHeapSize(uint64_t requestedSize) {
    uint64_t requestedMB = requestedSize / (1024 * 1024);
    uint64_t grantedMB = std::min(requestedMB, g_Device.safeHeapLimitMB);
    writeLog("[OS] nn::os::SetMemoryHeapSize(diminta=%" PRIu64 " MB -> dialokasikan=%" PRIu64 " MB)",
             requestedMB, grantedMB);
    return 0;
}

#include "logger.h"
#include "device_profile.h"
#include "tls_bridge.h"
#include <cstdint>

extern "C" uint32_t shim_nn_fs_MountRom(const char* name, void* cacheBuffer, uint64_t cacheSize) {
    ScopedAndroidTls tls;
    writeLog("[FS] nn::fs::MountRom(name=%s) -> streaming dari %s/romfs (buffer=%zu KB)",
             name, g_BaseDataDir.c_str(), g_Device.fsStreamBufferKB);
    return 0;
}

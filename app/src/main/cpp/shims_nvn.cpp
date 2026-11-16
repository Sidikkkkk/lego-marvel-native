#include "logger.h"
#include "device_profile.h"

extern "C" void* shim_nvnBootstrapLoader(const char* procName) {
    writeLog("[NVN] nvnBootstrapLoader mencari fungsi: %s", procName);
    return nullptr;
}

#include "logger.h"
#include "device_profile.h"
#include "tls_bridge.h"

extern "C" void* shim_nvnBootstrapLoader(const char* procName) {
    ScopedAndroidTls tls;
    writeLog("[NVN] nvnBootstrapLoader mencari fungsi: %s", procName);
    return nullptr;
}

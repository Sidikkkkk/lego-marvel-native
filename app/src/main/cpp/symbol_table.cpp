#include "symbol_table.h"
#include "logger.h"
#include <unordered_map>
#include <string>
#include <cstdint>

extern "C" {
    void shim_nn_init_Start();
    uint32_t shim_nn_os_SetMemoryHeapSize(uint64_t requestedSize);
    uint32_t shim_nn_fs_MountRom(const char* name, void* cacheBuffer, uint64_t cacheSize);
    void* shim_nvnBootstrapLoader(const char* procName);
}

extern "C" uint32_t shim_default_stub() {
    return 0;
}

static std::unordered_map<std::string, void*> g_Symbols = {
    {"_ZN2nn4init5StartEv", reinterpret_cast<void*>(&shim_nn_init_Start)},
    {"_ZN2nn2os17SetMemoryHeapSizeEm", reinterpret_cast<void*>(&shim_nn_os_SetMemoryHeapSize)},
    {"_ZN2nn2fs8MountRomEPKcPvm", reinterpret_cast<void*>(&shim_nn_fs_MountRom)},
    {"nvnBootstrapLoader", reinterpret_cast<void*>(&shim_nvnBootstrapLoader)},
};

void* resolveSymbol(const char* name) {
    auto it = g_Symbols.find(name);
    if (it != g_Symbols.end()) {
        return it->second;
    }
    writeLog("[MISSING SYMBOL] %s", name);
    return reinterpret_cast<void*>(&shim_default_stub);
}

#include "symbol_table.h"
#include "logger.h"
#include <dlfcn.h>
#include <cerrno>
#include <unordered_map>
#include <unordered_set>
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

extern "C" void shim_cxa_pure_virtual() {
    writeLog("[WARN] __cxa_pure_virtual dipanggil!");
}

extern "C" int shim_cxa_guard_acquire(uint64_t* guard) {
    if ((*reinterpret_cast<uint8_t*>(guard)) == 0) {
        return 1;
    }
    return 0;
}

extern "C" void shim_cxa_guard_release(uint64_t* guard) {
    *reinterpret_cast<uint8_t*>(guard) = 1;
}

extern "C" int* shim_errno_location() {
    return &errno;
}

static std::unordered_map<std::string, void*> g_Symbols = {
    {"_ZN2nn4init5StartEv", reinterpret_cast<void*>(&shim_nn_init_Start)},
    {"_ZN2nn2os17SetMemoryHeapSizeEm", reinterpret_cast<void*>(&shim_nn_os_SetMemoryHeapSize)},
    {"_ZN2nn2fs8MountRomEPKcPvm", reinterpret_cast<void*>(&shim_nn_fs_MountRom)},
    {"nvnBootstrapLoader", reinterpret_cast<void*>(&shim_nvnBootstrapLoader)},
    {"__cxa_pure_virtual", reinterpret_cast<void*>(&shim_cxa_pure_virtual)},
    {"__cxa_guard_acquire", reinterpret_cast<void*>(&shim_cxa_guard_acquire)},
    {"__cxa_guard_release", reinterpret_cast<void*>(&shim_cxa_guard_release)},
    {"__errno_location", reinterpret_cast<void*>(&shim_errno_location)},
};

static std::unordered_set<std::string> g_LoggedMissing;

void* resolveSymbol(const char* name) {
    if (!name || name[0] == '\0') {
        return reinterpret_cast<void*>(&shim_default_stub);
    }

    auto it = g_Symbols.find(name);
    if (it != g_Symbols.end()) {
        return it->second;
    }

    // Sambungkan otomatis fungsi standar C / Math / POSIX dari Android libc & libm
    void* nativeSym = dlsym(RTLD_DEFAULT, name);
    if (nativeSym != nullptr) {
        g_Symbols[name] = nativeSym;
        return nativeSym;
    }

    if (g_LoggedMissing.insert(name).second) {
        writeLog("[MISSING SYMBOL] %s", name);
    }
    return reinterpret_cast<void*>(&shim_default_stub);
}

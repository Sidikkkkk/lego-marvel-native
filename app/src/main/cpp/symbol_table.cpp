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
    uint32_t shim_nn_os_AllocateMemoryBlock(uintptr_t* outAddress, size_t size);
    void shim_StandardAllocator_ctor(void* thisPtr);
    void shim_StandardAllocator_Init(void* thisPtr, void* addr, size_t size, bool isCacheEnable);
    void* shim_StandardAllocator_Alloc(void* thisPtr, size_t size);
    void* shim_StandardAllocator_AllocAligned(void* thisPtr, size_t size, size_t alignment);
    void* shim_StandardAllocator_Realloc(void* thisPtr, void* ptr, size_t newSize);
    void shim_StandardAllocator_Free(void* thisPtr, void* ptr);
    void shim_nn_os_InitializeMutex(void* mutexPtr, bool recursive, int lockLevel);
    void shim_nn_os_LockMutex(void* mutexPtr);
    void shim_nn_os_UnlockMutex(void* mutexPtr);
    int64_t shim_nn_os_GetSystemTick();
    int64_t shim_nn_os_GetSystemTickFrequency();
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
    {"_ZN2nn2os19AllocateMemoryBlockEPmm", reinterpret_cast<void*>(&shim_nn_os_AllocateMemoryBlock)},
    {"_ZN2nn3mem17StandardAllocatorC1Ev", reinterpret_cast<void*>(&shim_StandardAllocator_ctor)},
    {"_ZN2nn3mem17StandardAllocator10InitializeEPvmb", reinterpret_cast<void*>(&shim_StandardAllocator_Init)},
    {"_ZN2nn3mem17StandardAllocator8AllocateEm", reinterpret_cast<void*>(&shim_StandardAllocator_Alloc)},
    {"_ZN2nn3mem17StandardAllocator8AllocateEmm", reinterpret_cast<void*>(&shim_StandardAllocator_AllocAligned)},
    {"_ZN2nn3mem17StandardAllocator10ReallocateEPvm", reinterpret_cast<void*>(&shim_StandardAllocator_Realloc)},
    {"_ZN2nn3mem17StandardAllocator4FreeEPv", reinterpret_cast<void*>(&shim_StandardAllocator_Free)},
    {"_ZN2nn2os15InitializeMutexEPNS0_9MutexTypeEbi", reinterpret_cast<void*>(&shim_nn_os_InitializeMutex)},
    {"_ZN2nn2os9LockMutexEPNS0_9MutexTypeE", reinterpret_cast<void*>(&shim_nn_os_LockMutex)},
    {"_ZN2nn2os11UnlockMutexEPNS0_9MutexTypeE", reinterpret_cast<void*>(&shim_nn_os_UnlockMutex)},
    {"_ZN2nn2os13GetSystemTickEv", reinterpret_cast<void*>(&shim_nn_os_GetSystemTick)},
    {"_ZN2nn2os22GetSystemTickFrequencyEv", reinterpret_cast<void*>(&shim_nn_os_GetSystemTickFrequency)},
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

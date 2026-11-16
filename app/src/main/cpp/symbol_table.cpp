#include "symbol_table.h"
#include "logger.h"
#include <dlfcn.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <cstdint>

extern uint64_t getElfOffsetFromAddr(void* addr);

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
    void* shim_nn_os_GetCurrentThread();
    int64_t shim_nn_os_GetSystemTick();
    int64_t shim_nn_os_GetSystemTickFrequency();
    uint32_t shim_nn_fs_MountRom(const char* name, void* cacheBuffer, uint64_t cacheSize);
    void* shim_nvnBootstrapLoader(const char* procName);
}

extern "C" uint32_t shim_default_stub() {
    return 0;
}

extern "C" void shim_cxa_pure_virtual() {
    writeLog("[WARN] __cxa_pure_virtual dipanggil dari offset 0x%llx",
             (unsigned long long)getElfOffsetFromAddr(__builtin_return_address(0)));
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

extern "C" void shim_abort() {
    void* caller = __builtin_return_address(0);
    writeLog("[ABORT] Game memanggil abort() dari %p (Offset ELF: 0x%llx)",
             caller, (unsigned long long)getElfOffsetFromAddr(caller));
    closeLogger();
    _exit(1);
}

extern "C" void shim_exit(int code) {
    void* caller = __builtin_return_address(0);
    writeLog("[EXIT] Game memanggil exit(%d) dari %p (Offset ELF: 0x%llx)",
             code, caller, (unsigned long long)getElfOffsetFromAddr(caller));
    closeLogger();
    _exit(code);
}

// Implementasi ABI libc++ std::__1::basic_string<char>::assign(const char*, size_t)
extern "C" void* shim_libcxx_string_assign(void* thisPtr, const char* s, size_t len) {
    if (!thisPtr) return nullptr;
    auto* bytes = reinterpret_cast<uint8_t*>(thisPtr);
    auto* words = reinterpret_cast<uint64_t*>(thisPtr);

    if ((bytes[0] & 1) != 0 && words[2] != 0) {
        free(reinterpret_cast<void*>(words[2]));
    }

    if (len <= 22) {
        memset(bytes, 0, 24);
        bytes[0] = static_cast<uint8_t>(len << 1);
        if (s && len > 0) memcpy(bytes + 1, s, len);
    } else {
        size_t cap = (len + 16) & ~15ULL;
        char* buf = reinterpret_cast<char*>(malloc(cap));
        if (buf) {
            if (s) memcpy(buf, s, len);
            buf[len] = '\0';
        }
        words[0] = cap | 1ULL;
        words[1] = len;
        words[2] = reinterpret_cast<uint64_t>(buf);
    }
    return thisPtr;
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
    {"_ZN2nn2os16GetCurrentThreadEv", reinterpret_cast<void*>(&shim_nn_os_GetCurrentThread)},
    {"_ZN2nn2os13GetSystemTickEv", reinterpret_cast<void*>(&shim_nn_os_GetSystemTick)},
    {"_ZN2nn2os22GetSystemTickFrequencyEv", reinterpret_cast<void*>(&shim_nn_os_GetSystemTickFrequency)},
    {"_ZN2nn2fs8MountRomEPKcPvm", reinterpret_cast<void*>(&shim_nn_fs_MountRom)},
    {"_ZNSt3__112basic_stringIcNS_11char_traitsIcEENS_9allocatorIcEEE6assignEPKcm", reinterpret_cast<void*>(&shim_libcxx_string_assign)},
    {"nvnBootstrapLoader", reinterpret_cast<void*>(&shim_nvnBootstrapLoader)},
    {"__cxa_pure_virtual", reinterpret_cast<void*>(&shim_cxa_pure_virtual)},
    {"__cxa_guard_acquire", reinterpret_cast<void*>(&shim_cxa_guard_acquire)},
    {"__cxa_guard_release", reinterpret_cast<void*>(&shim_cxa_guard_release)},
    {"__errno_location", reinterpret_cast<void*>(&shim_errno_location)},
    {"abort", reinterpret_cast<void*>(&shim_abort)},
    {"exit", reinterpret_cast<void*>(&shim_exit)},
    {"_exit", reinterpret_cast<void*>(&shim_exit)},
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

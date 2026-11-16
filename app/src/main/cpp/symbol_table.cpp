#include "symbol_table.h"
#include "tls_bridge.h"
#include "logger.h"
#include <sys/mman.h>
#include <dlfcn.h>
#include <unistd.h>
#include <cerrno>
#include <cstdio>
#include <cstdarg>
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
    void shim_nn_os_InitializeEvent(void* eventPtr, bool signaled, int clearMode);
    void shim_nn_os_WaitEvent(void* eventPtr);
    void shim_nn_os_SignalEvent(void* eventPtr);
    void shim_nn_os_ClearEvent(void* eventPtr);
    void shim_nn_os_FinalizeEvent(void* eventPtr);
    void shim_nn_os_InitializeSemaphore(void* semaPtr, int initialCount, int maxCount);
    void shim_nn_os_AcquireSemaphore(void* semaPtr);
    bool shim_nn_os_TryAcquireSemaphore(void* semaPtr);
    void shim_nn_os_ReleaseSemaphore(void* semaPtr, int count);
    void shim_nn_os_FinalizeSemaphore(void* semaPtr);
    uint32_t shim_nn_os_CreateThread(void* thread, void* fn, void* arg, void* stack, size_t stackSize, int priority);
    uint32_t shim_nn_os_CreateThreadCore(void* thread, void* fn, void* arg, void* stack, size_t stackSize, int priority, int core);
    void shim_nn_os_StartThread(void* thread);
    void* shim_nn_os_GetCurrentThread();
    int shim_nn_os_GetHostArgc();
    const char** shim_nn_os_GetHostArgv();
    void shim_nn_os_SleepThread(int64_t nanoSeconds);
    void shim_nn_os_YieldThread();
    int64_t shim_nn_os_GetSystemTick();
    int64_t shim_nn_os_GetSystemTickFrequency();
    uint32_t shim_nn_time_Initialize();
    void shim_nn_fs_SetAllocator(void* allocFn, void* deallocFn);
    void shim_nv_SetGraphicsAllocator(void* allocFn, void* freeFn, void* reallocFn, void* userData);
    uint32_t shim_nv_InitializeGraphics(void* addr, size_t size);
    void shim_movie_SetAllocator(void* allocFn, void* freeFn, void* reallocFn, void* userData);
    uint32_t shim_nn_fs_MountRom(const char* name, void* cacheBuffer, uint64_t cacheSize);
    void* shim_nvnBootstrapLoader(const char* procName);
}

static FILE* g_NullFile = nullptr;
static void* g_TerminateHandler = nullptr;

static FILE* getSafeStdFile() {
    ScopedAndroidTls tls;
    if (!g_NullFile) {
        g_NullFile = fopen("/dev/null", "w+");
    }
    return g_NullFile ? g_NullFile : stderr;
}

extern "C" uint32_t shim_default_stub() {
    return 0;
}

extern "C" void* shim_std_set_terminate(void* fn) {
    void* prev = g_TerminateHandler;
    g_TerminateHandler = fn;
    return prev;
}

extern "C" void shim_cxa_pure_virtual() {
    ScopedAndroidTls tls;
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
    ScopedAndroidTls tls;
    return &errno;
}

extern "C" void shim_abort() {
    ScopedAndroidTls tls;
    void* caller = __builtin_return_address(0);
    writeLog("[ABORT] Game memanggil abort() dari %p (Offset ELF: 0x%llx)",
             caller, (unsigned long long)getElfOffsetFromAddr(caller));
    closeLogger();
    _exit(1);
}

extern "C" void shim_exit(int code) {
    ScopedAndroidTls tls;
    void* caller = __builtin_return_address(0);
    writeLog("[EXIT] Game memanggil exit(%d) dari %p (Offset ELF: 0x%llx)",
             code, caller, (unsigned long long)getElfOffsetFromAddr(caller));
    closeLogger();
    _exit(code);
}

static void* allocLazyVirtualBlock(size_t alignment, size_t size) {
    size_t pageAlign = (alignment > 4096) ? alignment : 4096;
    size_t allocSize = (size + pageAlign + 4095) & ~4095ULL;
    void* raw = mmap(nullptr, allocSize, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (raw == MAP_FAILED) return nullptr;

    uintptr_t rawAddr = reinterpret_cast<uintptr_t>(raw);
    uintptr_t alignedAddr = (rawAddr + pageAlign - 1) & ~(pageAlign - 1);
    return reinterpret_cast<void*>(alignedAddr);
}

extern "C" void* shim_malloc(size_t size) {
    ScopedAndroidTls tls;
    if (size >= 32 * 1024 * 1024) {
        void* p = allocLazyVirtualBlock(4096, size);
        writeLog("[ALLOC-LAZY] malloc(%.2f MB) -> %p", size / (1024.0 * 1024.0), p);
        return p;
    }
    return malloc(size);
}

extern "C" void* shim_calloc(size_t nmemb, size_t size) {
    ScopedAndroidTls tls;
    size_t total = nmemb * size;
    if (total >= 32 * 1024 * 1024) {
        void* p = allocLazyVirtualBlock(4096, total);
        writeLog("[ALLOC-LAZY] calloc(%.2f MB) -> %p", total / (1024.0 * 1024.0), p);
        return p;
    }
    return calloc(nmemb, size);
}

extern "C" void* shim_realloc(void* ptr, size_t size) {
    ScopedAndroidTls tls;
    if (size >= 32 * 1024 * 1024) {
        void* p = allocLazyVirtualBlock(4096, size);
        writeLog("[ALLOC-LAZY] realloc(%.2f MB) -> %p", size / (1024.0 * 1024.0), p);
        return p;
    }
    return realloc(ptr, size);
}

extern "C" void shim_free(void* ptr) {
    ScopedAndroidTls tls;
    if (!ptr) return;
    uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
    if ((addr & 0xFFF) == 0 && addr >= 0x7000000000ULL) {
        return;
    }
    free(ptr);
}

extern "C" int shim_posix_memalign(void** memptr, size_t alignment, size_t size) {
    ScopedAndroidTls tls;
    if (!memptr) return EINVAL;
    if (size >= 32 * 1024 * 1024) {
        void* p = allocLazyVirtualBlock(alignment, size);
        writeLog("[ALLOC-LAZY] posix_memalign(%.2f MB) -> %p", size / (1024.0 * 1024.0), p);
        if (!p) return ENOMEM;
        *memptr = p;
        return 0;
    }
    return posix_memalign(memptr, alignment, size);
}

extern "C" void* shim_aligned_alloc(size_t alignment, size_t size) {
    ScopedAndroidTls tls;
    if (size >= 32 * 1024 * 1024) {
        void* p = allocLazyVirtualBlock(alignment, size);
        writeLog("[ALLOC-LAZY] aligned_alloc(%.2f MB) -> %p", size / (1024.0 * 1024.0), p);
        return p;
    }
    if (alignment < sizeof(void*)) alignment = sizeof(void*);
    void* ptr = nullptr;
    if (posix_memalign(&ptr, alignment, size > 0 ? size : alignment) != 0) return nullptr;
    return ptr;
}

extern "C" int shim_printf(const char* format, ...) {
    ScopedAndroidTls tls;
    if (!format) return 0;
    char buf[1024];
    va_list ap;
    va_start(ap, format);
    int ret = vsnprintf(buf, sizeof(buf), format, ap);
    va_end(ap);
    if (ret > 0) {
        size_t len = strlen(buf);
        if (len > 0 && buf[len - 1] == '\n') buf[len - 1] = '\0';
        writeLog("[GAME-STDOUT] %s", buf);
    }
    return ret;
}

extern "C" int shim_vprintf(const char* format, va_list ap) {
    ScopedAndroidTls tls;
    if (!format) return 0;
    char buf[1024];
    int ret = vsnprintf(buf, sizeof(buf), format, ap);
    if (ret > 0) {
        size_t len = strlen(buf);
        if (len > 0 && buf[len - 1] == '\n') buf[len - 1] = '\0';
        writeLog("[GAME-STDOUT] %s", buf);
    }
    return ret;
}

extern "C" int shim_puts(const char* s) {
    ScopedAndroidTls tls;
    if (s) writeLog("[GAME-STDOUT] %s", s);
    return 0;
}

extern "C" int shim_putchar(int c) {
    return c;
}

extern "C" int shim_fflush(FILE* stream) {
    return 0;
}

extern "C" size_t shim_fwrite(const void* ptr, size_t size, size_t nmemb, FILE* stream) {
    ScopedAndroidTls tls;
    if (!stream) stream = getSafeStdFile();
    return fwrite(ptr, size, nmemb, stream);
}

extern "C" int shim_fprintf(FILE* stream, const char* format, ...) {
    ScopedAndroidTls tls;
    if (!stream) stream = getSafeStdFile();
    va_list ap;
    va_start(ap, format);
    int ret = vfprintf(stream, format, ap);
    va_end(ap);
    return ret;
}

extern "C" int shim_vsnprintf(char* str, size_t size, const char* format, va_list ap) {
    ScopedAndroidTls tls;
    return vsnprintf(str, size, format, ap);
}

extern "C" int shim_snprintf(char* str, size_t size, const char* format, ...) {
    ScopedAndroidTls tls;
    va_list ap;
    va_start(ap, format);
    int ret = vsnprintf(str, size, format, ap);
    va_end(ap);
    return ret;
}

extern "C" int shim_vsprintf(char* str, const char* format, va_list ap) {
    ScopedAndroidTls tls;
    return vsprintf(str, format, ap);
}

extern "C" int shim_sprintf(char* str, const char* format, ...) {
    ScopedAndroidTls tls;
    va_list ap;
    va_start(ap, format);
    int ret = vsprintf(str, format, ap);
    va_end(ap);
    return ret;
}

extern "C" int shim_sscanf(const char* str, const char* format, ...) {
    ScopedAndroidTls tls;
    va_list ap;
    va_start(ap, format);
    int ret = vsscanf(str, format, ap);
    va_end(ap);
    return ret;
}

extern "C" double shim_strtod(const char* nptr, char** endptr) {
    ScopedAndroidTls tls;
    return strtod(nptr, endptr);
}

extern "C" void* shim_libcxx_string_assign(void* thisPtr, const char* s, size_t len) {
    ScopedAndroidTls tls;
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
    {"_ZN2nn2os15InitializeEventEPNS0_9EventTypeEbNS0_14EventClearModeE", reinterpret_cast<void*>(&shim_nn_os_InitializeEvent)},
    {"_ZN2nn2os9WaitEventEPNS0_9EventTypeE", reinterpret_cast<void*>(&shim_nn_os_WaitEvent)},
    {"_ZN2nn2os11SignalEventEPNS0_9EventTypeE", reinterpret_cast<void*>(&shim_nn_os_SignalEvent)},
    {"_ZN2nn2os10ClearEventEPNS0_9EventTypeE", reinterpret_cast<void*>(&shim_nn_os_ClearEvent)},
    {"_ZN2nn2os13FinalizeEventEPNS0_9EventTypeE", reinterpret_cast<void*>(&shim_nn_os_FinalizeEvent)},
    {"_ZN2nn2os19InitializeSemaphoreEPNS0_13SemaphoreTypeEii", reinterpret_cast<void*>(&shim_nn_os_InitializeSemaphore)},
    {"_ZN2nn2os16AcquireSemaphoreEPNS0_13SemaphoreTypeE", reinterpret_cast<void*>(&shim_nn_os_AcquireSemaphore)},
    {"_ZN2nn2os19TryAcquireSemaphoreEPNS0_13SemaphoreTypeE", reinterpret_cast<void*>(&shim_nn_os_TryAcquireSemaphore)},
    {"_ZN2nn2os16ReleaseSemaphoreEPNS0_13SemaphoreTypeEi", reinterpret_cast<void*>(&shim_nn_os_ReleaseSemaphore)},
    {"_ZN2nn2os17FinalizeSemaphoreEPNS0_13SemaphoreTypeE", reinterpret_cast<void*>(&shim_nn_os_FinalizeSemaphore)},
    {"_ZN2nn2os12CreateThreadEPNS0_10ThreadTypeEPFvPvES3_S3_mi", reinterpret_cast<void*>(&shim_nn_os_CreateThread)},
    {"_ZN2nn2os12CreateThreadEPNS0_10ThreadTypeEPFvPvES3_S3_mii", reinterpret_cast<void*>(&shim_nn_os_CreateThreadCore)},
    {"_ZN2nn2os11StartThreadEPNS0_10ThreadTypeE", reinterpret_cast<void*>(&shim_nn_os_StartThread)},
    {"_ZN2nn2os16GetCurrentThreadEv", reinterpret_cast<void*>(&shim_nn_os_GetCurrentThread)},
    {"_ZN2nn2os11GetHostArgcEv", reinterpret_cast<void*>(&shim_nn_os_GetHostArgc)},
    {"_ZN2nn2os11GetHostArgvEv", reinterpret_cast<void*>(&shim_nn_os_GetHostArgv)},
    {"_ZN2nn2os11SleepThreadENS_8TimeSpanE", reinterpret_cast<void*>(&shim_nn_os_SleepThread)},
    {"_ZN2nn2os11YieldThreadEv", reinterpret_cast<void*>(&shim_nn_os_YieldThread)},
    {"_ZN2nn2os13GetSystemTickEv", reinterpret_cast<void*>(&shim_nn_os_GetSystemTick)},
    {"_ZN2nn2os22GetSystemTickFrequencyEv", reinterpret_cast<void*>(&shim_nn_os_GetSystemTickFrequency)},
    {"_ZN2nn4time10InitializeEv", reinterpret_cast<void*>(&shim_nn_time_Initialize)},
    {"_ZN2nn2fs12SetAllocatorEPFPvmEPFvS1_mE", reinterpret_cast<void*>(&shim_nn_fs_SetAllocator)},
    {"_ZN2nv20SetGraphicsAllocatorEPFPvmmS0_EPFvS0_S0_EPFS0_S0_mS0_ES0_", reinterpret_cast<void*>(&shim_nv_SetGraphicsAllocator)},
    {"_ZN2nv18InitializeGraphicsEPvm", reinterpret_cast<void*>(&shim_nv_InitializeGraphics)},
    {"_ZN5movie12SetAllocatorEPFPvmmS0_EPFvS0_S0_EPFS0_S0_mS0_ES0_", reinterpret_cast<void*>(&shim_movie_SetAllocator)},
    {"_ZN2nn2fs8MountRomEPKcPvm", reinterpret_cast<void*>(&shim_nn_fs_MountRom)},
    {"_ZNSt3__112basic_stringIcNS_11char_traitsIcEENS_9allocatorIcEEE6assignEPKcm", reinterpret_cast<void*>(&shim_libcxx_string_assign)},
    {"_ZSt13set_terminatePFvvE", reinterpret_cast<void*>(&shim_std_set_terminate)},
    {"nvnBootstrapLoader", reinterpret_cast<void*>(&shim_nvnBootstrapLoader)},
    {"__cxa_pure_virtual", reinterpret_cast<void*>(&shim_cxa_pure_virtual)},
    {"__cxa_guard_acquire", reinterpret_cast<void*>(&shim_cxa_guard_acquire)},
    {"__cxa_guard_release", reinterpret_cast<void*>(&shim_cxa_guard_release)},
    {"__errno_location", reinterpret_cast<void*>(&shim_errno_location)},
    {"abort", reinterpret_cast<void*>(&shim_abort)},
    {"exit", reinterpret_cast<void*>(&shim_exit)},
    {"_exit", reinterpret_cast<void*>(&shim_exit)},
    {"_Exit", reinterpret_cast<void*>(&shim_exit)},
    {"malloc", reinterpret_cast<void*>(&shim_malloc)},
    {"calloc", reinterpret_cast<void*>(&shim_calloc)},
    {"realloc", reinterpret_cast<void*>(&shim_realloc)},
    {"free", reinterpret_cast<void*>(&shim_free)},
    {"posix_memalign", reinterpret_cast<void*>(&shim_posix_memalign)},
    {"aligned_alloc", reinterpret_cast<void*>(&shim_aligned_alloc)},
    {"printf", reinterpret_cast<void*>(&shim_printf)},
    {"vprintf", reinterpret_cast<void*>(&shim_vprintf)},
    {"puts", reinterpret_cast<void*>(&shim_puts)},
    {"putchar", reinterpret_cast<void*>(&shim_putchar)},
    {"fflush", reinterpret_cast<void*>(&shim_fflush)},
    {"fwrite", reinterpret_cast<void*>(&shim_fwrite)},
    {"fprintf", reinterpret_cast<void*>(&shim_fprintf)},
    {"snprintf", reinterpret_cast<void*>(&shim_snprintf)},
    {"vsnprintf", reinterpret_cast<void*>(&shim_vsnprintf)},
    {"sprintf", reinterpret_cast<void*>(&shim_sprintf)},
    {"vsprintf", reinterpret_cast<void*>(&shim_vsprintf)},
    {"sscanf", reinterpret_cast<void*>(&shim_sscanf)},
    {"strtod", reinterpret_cast<void*>(&shim_strtod)},
};

static std::unordered_set<std::string> g_LoggedMissing;

void* resolveSymbol(const char* name) {
    if (!name || name[0] == '\0') {
        return reinterpret_cast<void*>(&shim_default_stub);
    }

    if (strcmp(name, "stdin") == 0 || strcmp(name, "stdout") == 0 || strcmp(name, "stderr") == 0) {
        return getSafeStdFile();
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

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
    size_t shim_StandardAllocator_GetSizeOf(const void* thisPtr, const void* ptr);
    void shim_nn_os_InitializeMutex(void* mutexPtr, bool recursive, int lockLevel);
    void shim_nn_os_LockMutex(void* mutexPtr);
    bool shim_nn_os_TryLockMutex(void* mutexPtr);
    void shim_nn_os_UnlockMutex(void* mutexPtr);
    void shim_nn_os_FinalizeMutex(void* mutexPtr);
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
    uint32_t shim_nn_time_GetCurrentTime(int64_t* outPosixTime);
    uint32_t shim_nn_time_ToCalendarTimeUtc(void* outCal, const int64_t* posixTime);
    uint32_t shim_nn_time_ToCalendarTimeLocal(void* outCal, void* outAddInfo, const int64_t* posixTime);
    void shim_nn_vi_Initialize();
    uint32_t shim_nn_vi_OpenDefaultDisplay(void** outDisplay);
    uint32_t shim_nn_vi_CreateLayer(void** outLayer, void* display, int width, int height);
    uint32_t shim_nn_vi_GetNativeWindow(void** outWindow, void* layer);
    int shim_nn_oe_GetOperationMode();
    int shim_nn_oe_GetPerformanceMode();
    int shim_nn_oe_GetCurrentFocusState();
    uint32_t shim_nn_oe_PopNotificationMessage();
    uint64_t shim_nn_oe_GetDesiredLanguage();
    uint64_t shim_nn_settings_LanguageCode_Make(int language);
    bool shim_nn_settings_LanguageEq(const uint64_t* a, const uint64_t* b);
    void shim_nn_account_Initialize();
    bool shim_nn_account_TryOpenPreselectedUser(void* outUserHandle);
    uint32_t shim_nn_account_GetUserId(void* outUid, const void* userHandle);
    void shim_nn_hid_InitializeNpad();
    uint32_t shim_nn_hid_GetNpadStyleSet(const uint32_t* npadId);
    void shim_nn_hid_GetNpadState(void* outState, const uint32_t* npadId);
    int shim_nn_hid_GetHandles(void* outHandles, int count, const uint32_t* npadId, uint32_t styleSet);
    uint32_t shim_nn_hid_ShowControllerSupport(const void* arg);
    void shim_nn_fs_SetAllocator(void* allocFn, void* deallocFn);
    uint32_t shim_nn_fs_QueryMountRomCacheSize(size_t* outCacheSize);
    uint32_t shim_nn_fs_MountRom(const char* name, void* cacheBuffer, uint64_t cacheSize);
    uint32_t shim_nn_fs_MountSaveData(const char* name, const void* uid);
    uint32_t shim_nn_fs_EnsureSaveData(const void* uid);
    uint32_t shim_nn_fs_CommitSaveData(const char* name);
    uint32_t shim_nn_fs_OpenFile(void* outHandle, const char* path, int mode);
    uint32_t shim_nn_fs_GetFileSize(int64_t* outSize, void* handle);
    uint32_t shim_nn_fs_ReadFileOut(size_t* outBytesRead, void* handle, int64_t offset, void* buffer, size_t size);
    uint32_t shim_nn_fs_ReadFileNoOut(void* handle, int64_t offset, void* buffer, size_t size);
    uint32_t shim_nn_fs_WriteFile(void* handle, int64_t offset, const void* buffer, size_t size, const void* option);
    uint32_t shim_nn_fs_FlushFile(void* handle);
    void shim_nn_fs_CloseFile(void* handle);
    uint32_t shim_nn_fs_GetEntryType(uint32_t* outType, const char* path);
    uint32_t shim_nn_fs_OpenDirectory(void* outHandle, const char* path, int mode);
    uint32_t shim_nn_fs_ReadDirectory(int64_t* outCount, void* entryBuffer, void* handle, int64_t maxEntries);
    void shim_nn_fs_CloseDirectory(void* handle);
    uint32_t shim_nn_fs_CreateFile(const char* path, int64_t size);
    uint32_t shim_nn_fs_CreateDirectory(const char* path);
    uint32_t shim_nn_fs_DeleteFile(const char* path);
    uint32_t shim_nn_fs_RenameFile(const char* oldPath, const char* newPath);
    uint32_t shim_nn_fs_DeleteDirRecursive(const char* path);
    void shim_nv_SetGraphicsAllocator(void* allocFn, void* freeFn, void* reallocFn, void* userData);
    uint32_t shim_nv_InitializeGraphics(void* addr, size_t size);
    void shim_movie_SetAllocator(void* allocFn, void* freeFn, void* reallocFn, void* userData);
    void* shim_nvnBootstrapLoader(const char* procName);
    bool shim_BinaryFileHeader_IsValid(const void* thisPtr, int64_t expectedMagic, int majorVer, int minorVer, int microVer);
    bool shim_BinaryFileHeader_IsRelocated(const void* thisPtr);
    void* shim_BinaryFileHeader_GetFirstBlock(const void* thisPtr);
    void* shim_BinaryFileHeader_GetRelocationTable(const void* thisPtr);
    void shim_RelocationTable_Relocate(void* rltPtr);
    void* shim_nn_util_ReferSymbol(const void* sym);
    void shim_ApplicationErrorArg_ctor(void* thisPtr);
    void shim_ApplicationErrorArg_SetCode(void* thisPtr, uint32_t code);
    void shim_ApplicationErrorArg_SetDialogMsg(void* thisPtr, const char* msg);
    void shim_ApplicationErrorArg_SetFullMsg(void* thisPtr, const char* msg);
    void shim_ApplicationErrorArg_SetLang(void* thisPtr, const void* lang);
    void shim_nn_err_ShowApplicationError(const void* argPtr);
    void shim_nn_audio_InitParam(void* param);
    size_t shim_nn_audio_GetConfigWorkBufSize(const void* param);
    size_t shim_nn_audio_GetRendererWorkBufSize(const void* param);
    bool shim_nn_audio_IsValidParam(const void* param);
    void shim_nn_audio_InitConfig(void* config, const void* param, void* workBuf, size_t workBufSize);
    uint32_t shim_nn_audio_OpenRenderer(void* handle, void* sysEvent, const void* param, void* workBuf, size_t workBufSize);
    uint32_t shim_nn_audio_StartRenderer(void* handle);
    uint32_t shim_nn_audio_RequestUpdateRenderer(void* handle, const void* config);
    bool shim_nn_audio_AcquireTrue();
    bool shim_nn_audio_AppendWaveBuffer(void* voice, const void* waveBuf);
    const void* shim_nn_audio_GetReleasedWaveBuffer(void* voice);
    int shim_nn_audio_GetWaveBufferCount(const void* voice);
    int shim_nn_audio_GetVoicePlayState(const void* voice);
    uint8_t shim_glslc_Initialize();
    void shim_glslc_noop();
    int shim_nn_socket_Error();
    int shim_nn_socket_Ok();
    uint16_t shim_nn_socket_InetNtohs(uint16_t netshort);
    size_t shim_nn_swkbd_GetStringBufSize();
    size_t shim_nn_swkbd_GetWorkBufSize(bool enableDic);
    uint32_t shim_nn_swkbd_ShowKeyboard(void* outString, const void* arg);
    void shim_nn_UnexpectedDefaultImpl(const char* func, const char* file, int line);
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
    if (size >= 2 * 1024 * 1024) {
        void* p = allocLazyVirtualBlock(4096, size);
        writeLog("[ALLOC-LAZY] malloc(%.2f MB) -> %p", size / (1024.0 * 1024.0), p);
        return p;
    }
    return malloc(size);
}

extern "C" void* shim_calloc(size_t nmemb, size_t size) {
    ScopedAndroidTls tls;
    size_t total = nmemb * size;
    if (total >= 2 * 1024 * 1024) {
        void* p = allocLazyVirtualBlock(4096, total);
        writeLog("[ALLOC-LAZY] calloc(%.2f MB) -> %p", total / (1024.0 * 1024.0), p);
        return p;
    }
    return calloc(nmemb, size);
}

extern "C" void* shim_realloc(void* ptr, size_t size) {
    ScopedAndroidTls tls;
    if (size >= 2 * 1024 * 1024) {
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
    if (size >= 2 * 1024 * 1024) {
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
    if (size >= 2 * 1024 * 1024) {
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

extern "C" int shim_putchar(int c) { return c; }
extern "C" int shim_fflush(FILE* stream) { return 0; }

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
    {"_ZNK2nn3mem17StandardAllocator9GetSizeOfEPKv", reinterpret_cast<void*>(&shim_StandardAllocator_GetSizeOf)},
    {"_ZN2nn2os15InitializeMutexEPNS0_9MutexTypeEbi", reinterpret_cast<void*>(&shim_nn_os_InitializeMutex)},
    {"_ZN2nn2os9LockMutexEPNS0_9MutexTypeE", reinterpret_cast<void*>(&shim_nn_os_LockMutex)},
    {"_ZN2nn2os12TryLockMutexEPNS0_9MutexTypeE", reinterpret_cast<void*>(&shim_nn_os_TryLockMutex)},
    {"_ZN2nn2os11UnlockMutexEPNS0_9MutexTypeE", reinterpret_cast<void*>(&shim_nn_os_UnlockMutex)},
    {"_ZN2nn2os13FinalizeMutexEPNS0_9MutexTypeE", reinterpret_cast<void*>(&shim_nn_os_FinalizeMutex)},
    {"_ZN2nn2os15InitializeEventEPNS0_9EventTypeEbNS0_14EventClearModeE", reinterpret_cast<void*>(&shim_nn_os_InitializeEvent)},
    {"_ZN2nn2os9WaitEventEPNS0_9EventTypeE", reinterpret_cast<void*>(&shim_nn_os_WaitEvent)},
    {"_ZN2nn2os11SignalEventEPNS0_9EventTypeE", reinterpret_cast<void*>(&shim_nn_os_SignalEvent)},
    {"_ZN2nn2os10ClearEventEPNS0_9EventTypeE", reinterpret_cast<void*>(&shim_nn_os_ClearEvent)},
    {"_ZN2nn2os13FinalizeEventEPNS0_9EventTypeE", reinterpret_cast<void*>(&shim_nn_os_FinalizeEvent)},
    {"_ZN2nn2os18DestroySystemEventEPNS0_15SystemEventTypeE", reinterpret_cast<void*>(&shim_nn_os_FinalizeEvent)},
    {"_ZN2nn2os19InitializeSemaphoreEPNS0_13SemaphoreTypeEii", reinterpret_cast<void*>(&shim_nn_os_InitializeSemaphore)},
    {"_ZN2nn2os16AcquireSemaphoreEPNS0_13SemaphoreTypeE", reinterpret_cast<void*>(&shim_nn_os_AcquireSemaphore)},
    {"_ZN2nn2os19TryAcquireSemaphoreEPNS0_13SemaphoreTypeE", reinterpret_cast<void*>(&shim_nn_os_TryAcquireSemaphore)},
    {"_ZN2nn2os16ReleaseSemaphoreEPNS0_13SemaphoreTypeEi", reinterpret_cast<void*>(&shim_nn_os_ReleaseSemaphore)},
    {"_ZN2nn2os17FinalizeSemaphoreEPNS0_13SemaphoreTypeE", reinterpret_cast<void*>(&shim_nn_os_FinalizeSemaphore)},
    {"_ZN2nn2os12CreateThreadEPNS0_10ThreadTypeEPFvPvES3_S3_mi", reinterpret_cast<void*>(&shim_nn_os_CreateThread)},
    {"_ZN2nn2os12CreateThreadEPNS0_10ThreadTypeEPFvPvES3_S3_mii", reinterpret_cast<void*>(&shim_nn_os_CreateThreadCore)},
    {"_ZN2nn2os11StartThreadEPNS0_10ThreadTypeE", reinterpret_cast<void*>(&shim_nn_os_StartThread)},
    {"_ZN2nn2os13DestroyThreadEPNS0_10ThreadTypeE", reinterpret_cast<void*>(&shim_default_stub)},
    {"_ZN2nn2os17SetThreadCoreMaskEPNS0_10ThreadTypeEim", reinterpret_cast<void*>(&shim_default_stub)},
    {"_ZN2nn2os20ChangeThreadPriorityEPNS0_10ThreadTypeEi", reinterpret_cast<void*>(&shim_default_stub)},
    {"_ZN2nn2os13SetThreadNameEPNS0_10ThreadTypeEPKc", reinterpret_cast<void*>(&shim_default_stub)},
    {"_ZN2nn2os16GetCurrentThreadEv", reinterpret_cast<void*>(&shim_nn_os_GetCurrentThread)},
    {"_ZN2nn2os11GetHostArgcEv", reinterpret_cast<void*>(&shim_nn_os_GetHostArgc)},
    {"_ZN2nn2os11GetHostArgvEv", reinterpret_cast<void*>(&shim_nn_os_GetHostArgv)},
    {"_ZN2nn2os11SleepThreadENS_8TimeSpanE", reinterpret_cast<void*>(&shim_nn_os_SleepThread)},
    {"_ZN2nn2os11YieldThreadEv", reinterpret_cast<void*>(&shim_nn_os_YieldThread)},
    {"_ZN2nn2os13GetSystemTickEv", reinterpret_cast<void*>(&shim_nn_os_GetSystemTick)},
    {"_ZN2nn2os22GetSystemTickFrequencyEv", reinterpret_cast<void*>(&shim_nn_os_GetSystemTickFrequency)},
    {"_ZN2nn4time10InitializeEv", reinterpret_cast<void*>(&shim_nn_time_Initialize)},
    {"_ZN2nn4time23StandardUserSystemClock14GetCurrentTimeEPNS0_9PosixTimeE", reinterpret_cast<void*>(&shim_nn_time_GetCurrentTime)},
    {"_ZN2nn4time19ToCalendarTimeInUtcERKNS0_9PosixTimeE", reinterpret_cast<void*>(&shim_nn_time_ToCalendarTimeUtc)},
    {"_ZN2nn4time14ToCalendarTimeEPNS0_12CalendarTimeEPNS0_22CalendarAdditionalInfoERKNS0_9PosixTimeE", reinterpret_cast<void*>(&shim_nn_time_ToCalendarTimeLocal)},
    {"_ZN2nn2vi10InitializeEv", reinterpret_cast<void*>(&shim_nn_vi_Initialize)},
    {"_ZN2nn2vi18OpenDefaultDisplayEPPNS0_7DisplayE", reinterpret_cast<void*>(&shim_nn_vi_OpenDefaultDisplay)},
    {"_ZN2nn2vi11CreateLayerEPPNS0_5LayerEPNS0_7DisplayEii", reinterpret_cast<void*>(&shim_nn_vi_CreateLayer)},
    {"_ZN2nn2vi15GetNativeWindowEPPvPNS0_5LayerE", reinterpret_cast<void*>(&shim_nn_vi_GetNativeWindow)},
    {"_ZN2nn2oe16GetOperationModeEv", reinterpret_cast<void*>(&shim_nn_oe_GetOperationMode)},
    {"_ZN2nn2oe18GetPerformanceModeEv", reinterpret_cast<void*>(&shim_nn_oe_GetPerformanceMode)},
    {"_ZN2nn2oe20GetCurrentFocusStateEv", reinterpret_cast<void*>(&shim_nn_oe_GetCurrentFocusState)},
    {"_ZN2nn2oe22PopNotificationMessageEv", reinterpret_cast<void*>(&shim_nn_oe_PopNotificationMessage)},
    {"_ZN2nn2oe18GetDesiredLanguageEv", reinterpret_cast<void*>(&shim_nn_oe_GetDesiredLanguage)},
    {"_ZN2nn2oe31EnterExitRequestHandlingSectionEv", reinterpret_cast<void*>(&shim_default_stub)},
    {"_ZN2nn2oe31LeaveExitRequestHandlingSectionEv", reinterpret_cast<void*>(&shim_default_stub)},
    {"_ZN2nn2oe20SetFocusHandlingModeENS0_17FocusHandlingModeE", reinterpret_cast<void*>(&shim_default_stub)},
    {"_ZN2nn2oe28SetResumeNotificationEnabledEb", reinterpret_cast<void*>(&shim_default_stub)},
    {"_ZN2nn2oe44SetPerformanceModeChangedNotificationEnabledEb", reinterpret_cast<void*>(&shim_default_stub)},
    {"_ZN2nn2oe42SetOperationModeChangedNotificationEnabledEb", reinterpret_cast<void*>(&shim_default_stub)},
    {"_ZN2nn8settings12LanguageCode4MakeENS0_8LanguageE", reinterpret_cast<void*>(&shim_nn_settings_LanguageCode_Make)},
    {"_ZN2nn8settingseqERKNS0_12LanguageCodeERKNS0_8LanguageE", reinterpret_cast<void*>(&shim_nn_settings_LanguageEq)},
    {"_ZN2nn7account10InitializeEv", reinterpret_cast<void*>(&shim_nn_account_Initialize)},
    {"_ZN2nn7account22TryOpenPreselectedUserEPNS0_10UserHandleE", reinterpret_cast<void*>(&shim_nn_account_TryOpenPreselectedUser)},
    {"_ZN2nn7account9GetUserIdEPNS0_3UidERKNS0_10UserHandleE", reinterpret_cast<void*>(&shim_nn_account_GetUserId)},
    {"_ZN2nn3hid14InitializeNpadEv", reinterpret_cast<void*>(&shim_nn_hid_InitializeNpad)},
    {"_ZN2nn3hid15GetNpadStyleSetERKj", reinterpret_cast<void*>(&shim_nn_hid_GetNpadStyleSet)},
    {"_ZN2nn3hid24SetSupportedNpadStyleSetENS_4util10BitFlagSetILi32ENS0_12NpadStyleTagEEE", reinterpret_cast<void*>(&shim_default_stub)},
    {"_ZN2nn3hid22SetSupportedNpadIdTypeEPKjm", reinterpret_cast<void*>(&shim_default_stub)},
    {"_ZN2nn3hid18SetNpadJoyHoldTypeENS0_15NpadJoyHoldTypeE", reinterpret_cast<void*>(&shim_default_stub)},
    {"_ZN2nn3hid12GetNpadStateEPNS0_17NpadHandheldStateERKj", reinterpret_cast<void*>(&shim_nn_hid_GetNpadState)},
    {"_ZN2nn3hid12GetNpadStateEPNS0_16NpadFullKeyStateERKj", reinterpret_cast<void*>(&shim_nn_hid_GetNpadState)},
    {"_ZN2nn3hid12GetNpadStateEPNS0_16NpadJoyDualStateERKj", reinterpret_cast<void*>(&shim_nn_hid_GetNpadState)},
    {"_ZN2nn3hid12GetNpadStateEPNS0_16NpadJoyLeftStateERKj", reinterpret_cast<void*>(&shim_nn_hid_GetNpadState)},
    {"_ZN2nn3hid12GetNpadStateEPNS0_17NpadJoyRightStateERKj", reinterpret_cast<void*>(&shim_nn_hid_GetNpadState)},
    {"_ZN2nn3hid23GetSixAxisSensorHandlesEPNS0_19SixAxisSensorHandleEiRKjNS_4util10BitFlagSetILi32ENS0_12NpadStyleTagEEE", reinterpret_cast<void*>(&shim_nn_hid_GetHandles)},
    {"_ZN2nn3hid18StartSixAxisSensorERKNS0_19SixAxisSensorHandleE", reinterpret_cast<void*>(&shim_default_stub)},
    {"_ZN2nn3hid21GetSixAxisSensorStateEPNS0_18SixAxisSensorStateERKNS0_19SixAxisSensorHandleE", reinterpret_cast<void*>(&shim_default_stub)},
    {"_ZN2nn3hid25GetVibrationDeviceHandlesEPNS0_21VibrationDeviceHandleEiRKjNS_4util10BitFlagSetILi32ENS0_12NpadStyleTagEEE", reinterpret_cast<void*>(&shim_nn_hid_GetHandles)},
    {"_ZN2nn3hid25InitializeVibrationDeviceERKNS0_21VibrationDeviceHandleE", reinterpret_cast<void*>(&shim_default_stub)},
    {"_ZN2nn3hid18SendVibrationValueERKNS0_21VibrationDeviceHandleERKNS0_14VibrationValueE", reinterpret_cast<void*>(&shim_default_stub)},
    {"_ZN2nn3hid20ControllerSupportArg10SetDefaultEv", reinterpret_cast<void*>(&shim_default_stub)},
    {"_ZN2nn3hid21ShowControllerSupportERKNS0_20ControllerSupportArgE", reinterpret_cast<void*>(&shim_nn_hid_ShowControllerSupport)},
    {"_ZN2nn2fs12SetAllocatorEPFPvmEPFvS1_mE", reinterpret_cast<void*>(&shim_nn_fs_SetAllocator)},
    {"_ZN2nn2fs22QueryMountRomCacheSizeEPm", reinterpret_cast<void*>(&shim_nn_fs_QueryMountRomCacheSize)},
    {"_ZN2nn2fs8MountRomEPKcPvm", reinterpret_cast<void*>(&shim_nn_fs_MountRom)},
    {"_ZN2nn2fs7UnmountEPKc", reinterpret_cast<void*>(&shim_default_stub)},
    {"_ZN2nn2fs13MountSaveDataEPKcRKNS_7account3UidE", reinterpret_cast<void*>(&shim_nn_fs_MountSaveData)},
    {"_ZN2nn2fs14EnsureSaveDataERKNS_7account3UidE", reinterpret_cast<void*>(&shim_nn_fs_EnsureSaveData)},
    {"_ZN2nn2fs14CommitSaveDataEPKc", reinterpret_cast<void*>(&shim_nn_fs_CommitSaveData)},
    {"_ZN2nn2fs8OpenFileEPNS0_10FileHandleEPKci", reinterpret_cast<void*>(&shim_nn_fs_OpenFile)},
    {"_ZN2nn2fs11GetFileSizeEPlNS0_10FileHandleE", reinterpret_cast<void*>(&shim_nn_fs_GetFileSize)},
    {"_ZN2nn2fs8ReadFileEPmNS0_10FileHandleElPvm", reinterpret_cast<void*>(&shim_nn_fs_ReadFileOut)},
    {"_ZN2nn2fs8ReadFileENS0_10FileHandleElPvm", reinterpret_cast<void*>(&shim_nn_fs_ReadFileNoOut)},
    {"_ZN2nn2fs9WriteFileENS0_10FileHandleElPKvmRKNS0_11WriteOptionE", reinterpret_cast<void*>(&shim_nn_fs_WriteFile)},
    {"_ZN2nn2fs9FlushFileENS0_10FileHandleE", reinterpret_cast<void*>(&shim_nn_fs_FlushFile)},
    {"_ZN2nn2fs9CloseFileENS0_10FileHandleE", reinterpret_cast<void*>(&shim_nn_fs_CloseFile)},
    {"_ZN2nn2fs12GetEntryTypeEPNS0_18DirectoryEntryTypeEPKc", reinterpret_cast<void*>(&shim_nn_fs_GetEntryType)},
    {"_ZN2nn2fs13OpenDirectoryEPNS0_15DirectoryHandleEPKci", reinterpret_cast<void*>(&shim_nn_fs_OpenDirectory)},
    {"_ZN2nn2fs13ReadDirectoryEPlPNS0_14DirectoryEntryENS0_15DirectoryHandleEl", reinterpret_cast<void*>(&shim_nn_fs_ReadDirectory)},
    {"_ZN2nn2fs14CloseDirectoryENS0_15DirectoryHandleE", reinterpret_cast<void*>(&shim_nn_fs_CloseDirectory)},
    {"_ZN2nn2fs10CreateFileEPKcl", reinterpret_cast<void*>(&shim_nn_fs_CreateFile)},
    {"_ZN2nn2fs15CreateDirectoryEPKc", reinterpret_cast<void*>(&shim_nn_fs_CreateDirectory)},
    {"_ZN2nn2fs10DeleteFileEPKc", reinterpret_cast<void*>(&shim_nn_fs_DeleteFile)},
    {"_ZN2nn2fs15DeleteDirectoryEPKc", reinterpret_cast<void*>(&shim_nn_fs_DeleteFile)},
    {"_ZN2nn2fs26DeleteDirectoryRecursivelyEPKc", reinterpret_cast<void*>(&shim_nn_fs_DeleteDirRecursive)},
    {"_ZN2nn2fs10RenameFileEPKcS2_", reinterpret_cast<void*>(&shim_nn_fs_RenameFile)},
    {"_ZN2nv20SetGraphicsAllocatorEPFPvmmS0_EPFvS0_S0_EPFS0_S0_mS0_ES0_", reinterpret_cast<void*>(&shim_nv_SetGraphicsAllocator)},
    {"_ZN2nv18InitializeGraphicsEPvm", reinterpret_cast<void*>(&shim_nv_InitializeGraphics)},
    {"_ZN5movie12SetAllocatorEPFPvmmS0_EPFvS0_S0_EPFS0_S0_mS0_ES0_", reinterpret_cast<void*>(&shim_movie_SetAllocator)},
    {"_ZNK2nn4util16BinaryFileHeader7IsValidEliii", reinterpret_cast<void*>(&shim_BinaryFileHeader_IsValid)},
    {"_ZNK2nn4util16BinaryFileHeader11IsRelocatedEv", reinterpret_cast<void*>(&shim_BinaryFileHeader_IsRelocated)},
    {"_ZNK2nn4util16BinaryFileHeader13GetFirstBlockEv", reinterpret_cast<void*>(&shim_BinaryFileHeader_GetFirstBlock)},
    {"_ZN2nn4util16BinaryFileHeader13GetFirstBlockEv", reinterpret_cast<void*>(&shim_BinaryFileHeader_GetFirstBlock)},
    {"_ZN2nn4util16BinaryFileHeader18GetRelocationTableEv", reinterpret_cast<void*>(&shim_BinaryFileHeader_GetRelocationTable)},
    {"_ZN2nn4util15RelocationTable8RelocateEv", reinterpret_cast<void*>(&shim_RelocationTable_Relocate)},
    {"_ZN2nn4util11ReferSymbolEPKv", reinterpret_cast<void*>(&shim_nn_util_ReferSymbol)},
    {"_ZN2nn3err19ApplicationErrorArgC1Ev", reinterpret_cast<void*>(&shim_ApplicationErrorArg_ctor)},
    {"_ZN2nn3err19ApplicationErrorArg29SetApplicationErrorCodeNumberEj", reinterpret_cast<void*>(&shim_ApplicationErrorArg_SetCode)},
    {"_ZN2nn3err19ApplicationErrorArg16SetDialogMessageEPKc", reinterpret_cast<void*>(&shim_ApplicationErrorArg_SetDialogMsg)},
    {"_ZN2nn3err19ApplicationErrorArg20SetFullScreenMessageEPKc", reinterpret_cast<void*>(&shim_ApplicationErrorArg_SetFullMsg)},
    {"_ZN2nn3err19ApplicationErrorArg15SetLanguageCodeERKNS_8settings12LanguageCodeE", reinterpret_cast<void*>(&shim_ApplicationErrorArg_SetLang)},
    {"_ZN2nn3err20ShowApplicationErrorERKNS0_19ApplicationErrorArgE", reinterpret_cast<void*>(&shim_nn_err_ShowApplicationError)},
    {"_ZN2nn5audio32InitializeAudioRendererParameterEPNS0_22AudioRendererParameterE", reinterpret_cast<void*>(&shim_nn_audio_InitParam)},
    {"_ZN2nn5audio36GetAudioRendererConfigWorkBufferSizeERKNS0_22AudioRendererParameterE", reinterpret_cast<void*>(&shim_nn_audio_GetConfigWorkBufSize)},
    {"_ZN2nn5audio30GetAudioRendererWorkBufferSizeERKNS0_22AudioRendererParameterE", reinterpret_cast<void*>(&shim_nn_audio_GetRendererWorkBufSize)},
    {"_ZN2nn5audio29IsValidAudioRendererParameterERKNS0_22AudioRendererParameterE", reinterpret_cast<void*>(&shim_nn_audio_IsValidParam)},
    {"_ZN2nn5audio29InitializeAudioRendererConfigEPNS0_19AudioRendererConfigERKNS0_22AudioRendererParameterEPvm", reinterpret_cast<void*>(&shim_nn_audio_InitConfig)},
    {"_ZN2nn5audio17OpenAudioRendererEPNS0_19AudioRendererHandleEPNS_2os11SystemEventERKNS0_22AudioRendererParameterEPvm", reinterpret_cast<void*>(&shim_nn_audio_OpenRenderer)},
    {"_ZN2nn5audio18StartAudioRendererENS0_19AudioRendererHandleE", reinterpret_cast<void*>(&shim_nn_audio_StartRenderer)},
    {"_ZN2nn5audio26RequestUpdateAudioRendererENS0_19AudioRendererHandleEPKNS0_19AudioRendererConfigE", reinterpret_cast<void*>(&shim_nn_audio_RequestUpdateRenderer)},
    {"_ZN2nn5audio15AcquireFinalMixEPNS0_19AudioRendererConfigEPNS0_12FinalMixTypeEi", reinterpret_cast<void*>(&shim_nn_audio_AcquireTrue)},
    {"_ZN2nn5audio13AcquireSubMixEPNS0_19AudioRendererConfigEPNS0_10SubMixTypeEii", reinterpret_cast<void*>(&shim_nn_audio_AcquireTrue)},
    {"_ZN2nn5audio20SetSubMixDestinationEPNS0_19AudioRendererConfigEPNS0_10SubMixTypeEPNS0_12FinalMixTypeE", reinterpret_cast<void*>(&shim_default_stub)},
    {"_ZN2nn5audio18SetSubMixMixVolumeEPNS0_10SubMixTypeEPNS0_12FinalMixTypeEfii", reinterpret_cast<void*>(&shim_default_stub)},
    {"_ZN2nn5audio16AcquireVoiceSlotEPNS0_19AudioRendererConfigEPNS0_9VoiceTypeEiiNS0_12SampleFormatEiPKvm", reinterpret_cast<void*>(&shim_nn_audio_AcquireTrue)},
    {"_ZN2nn5audio16ReleaseVoiceSlotEPNS0_19AudioRendererConfigEPNS0_9VoiceTypeE", reinterpret_cast<void*>(&shim_default_stub)},
    {"_ZN2nn5audio19SetVoiceDestinationEPNS0_19AudioRendererConfigEPNS0_9VoiceTypeEPNS0_10SubMixTypeE", reinterpret_cast<void*>(&shim_default_stub)},
    {"_ZN2nn5audio17SetVoiceMixVolumeEPNS0_9VoiceTypeEPNS0_10SubMixTypeEfii", reinterpret_cast<void*>(&shim_default_stub)},
    {"_ZN2nn5audio13SetVoicePitchEPNS0_9VoiceTypeEf", reinterpret_cast<void*>(&shim_default_stub)},
    {"_ZN2nn5audio17SetVoicePlayStateEPNS0_9VoiceTypeENS1_9PlayStateE", reinterpret_cast<void*>(&shim_default_stub)},
    {"_ZN2nn5audio17GetVoicePlayStateEPKNS0_9VoiceTypeE", reinterpret_cast<void*>(&shim_nn_audio_GetVoicePlayState)},
    {"_ZN2nn5audio29SetVoiceBiquadFilterParameterEPNS0_9VoiceTypeEiRKNS0_21BiquadFilterParameterE", reinterpret_cast<void*>(&shim_default_stub)},
    {"_ZN2nn5audio16AppendWaveBufferEPNS0_9VoiceTypeEPKNS0_10WaveBufferE", reinterpret_cast<void*>(&shim_nn_audio_AppendWaveBuffer)},
    {"_ZN2nn5audio21GetReleasedWaveBufferEPNS0_9VoiceTypeE", reinterpret_cast<void*>(&shim_nn_audio_GetReleasedWaveBuffer)},
    {"_ZN2nn5audio18GetWaveBufferCountEPKNS0_9VoiceTypeE", reinterpret_cast<void*>(&shim_nn_audio_GetWaveBufferCount)},
    {"_ZN2nn5audio12IsVoiceValidEPKNS0_9VoiceTypeE", reinterpret_cast<void*>(&shim_nn_audio_AcquireTrue)},
    {"_ZN2nn5audio25GetVoicePlayedSampleCountEPKNS0_9VoiceTypeE", reinterpret_cast<void*>(&shim_default_stub)},
    {"_ZN2nn5audio17AcquireMemoryPoolEPNS0_19AudioRendererConfigEPNS0_14MemoryPoolTypeEPvm", reinterpret_cast<void*>(&shim_nn_audio_AcquireTrue)},
    {"_ZN2nn5audio23RequestAttachMemoryPoolEPNS0_14MemoryPoolTypeE", reinterpret_cast<void*>(&shim_nn_audio_AcquireTrue)},
    {"_ZN2nn5audio13AddDeviceSinkEPNS0_19AudioRendererConfigEPNS0_14DeviceSinkTypeEPNS0_12FinalMixTypeEPKaiPKc", reinterpret_cast<void*>(&shim_nn_audio_AcquireTrue)},
    {"_ZN2nn5audio19ListAudioDeviceNameEPNS0_15AudioDeviceNameEi", reinterpret_cast<void*>(&shim_default_stub)},
    {"_ZN2nn5audio26SetAudioDeviceOutputVolumeEPKNS0_15AudioDeviceNameEf", reinterpret_cast<void*>(&shim_default_stub)},
    {"_ZN2nn5audio16ParseAdpcmHeaderEPNS0_15AdpcmHeaderInfoEPKvm", reinterpret_cast<void*>(&shim_default_stub)},
    {"_ZN2nn5codec11OpusDecoderC1Ev", reinterpret_cast<void*>(&shim_default_stub)},
    {"glslcInitialize", reinterpret_cast<void*>(&shim_glslc_Initialize)},
    {"glslcFinalize", reinterpret_cast<void*>(&shim_glslc_noop)},
    {"glslcSetAllocator", reinterpret_cast<void*>(&shim_glslc_noop)},
    {"glslcGetDefaultOptions", reinterpret_cast<void*>(&shim_glslc_noop)},
    {"glslcGetVersion", reinterpret_cast<void*>(&shim_glslc_noop)},
    {"glslcCompile", reinterpret_cast<void*>(&shim_glslc_Initialize)},
    {"glslcCompileSpecialized", reinterpret_cast<void*>(&shim_glslc_Initialize)},
    {"glslcCompilePreSpecialized", reinterpret_cast<void*>(&shim_glslc_Initialize)},
    {"_ZN2nn6socket6SocketEiii", reinterpret_cast<void*>(&shim_nn_socket_Error)},
    {"_ZN2nn6socket5CloseEi", reinterpret_cast<void*>(&shim_nn_socket_Ok)},
    {"_ZN2nn6socket7ConnectEiPK8sockaddrj", reinterpret_cast<void*>(&shim_nn_socket_Error)},
    {"_ZN2nn6socket4BindEiPK8sockaddrj", reinterpret_cast<void*>(&shim_nn_socket_Error)},
    {"_ZN2nn6socket6ListenEii", reinterpret_cast<void*>(&shim_nn_socket_Error)},
    {"_ZN2nn6socket6AcceptEiP8sockaddrPj", reinterpret_cast<void*>(&shim_nn_socket_Error)},
    {"_ZN2nn6socket4SendEiPKvmi", reinterpret_cast<void*>(&shim_nn_socket_Error)},
    {"_ZN2nn6socket4RecvEiPvmi", reinterpret_cast<void*>(&shim_nn_socket_Error)},
    {"_ZN2nn6socket6SendToEiPKvmiPK8sockaddrj", reinterpret_cast<void*>(&shim_nn_socket_Error)},
    {"_ZN2nn6socket8RecvFromEiPvmiP8sockaddrPj", reinterpret_cast<void*>(&shim_nn_socket_Error)},
    {"_ZN2nn6socket8ShutdownEii", reinterpret_cast<void*>(&shim_nn_socket_Ok)},
    {"_ZN2nn6socket10SetSockOptEiiiPKvj", reinterpret_cast<void*>(&shim_nn_socket_Ok)},
    {"_ZN2nn6socket11GetPeerNameEiP8sockaddrPj", reinterpret_cast<void*>(&shim_nn_socket_Error)},
    {"_ZN2nn6socket8InetPtonEiPKcPv", reinterpret_cast<void*>(&shim_nn_socket_Ok)},
    {"_ZN2nn6socket9InetNtohsEt", reinterpret_cast<void*>(&shim_nn_socket_InetNtohs)},
    {"_ZN2nn5swkbd27GetRequiredStringBufferSizeEv", reinterpret_cast<void*>(&shim_nn_swkbd_GetStringBufSize)},
    {"_ZN2nn5swkbd25GetRequiredWorkBufferSizeEb", reinterpret_cast<void*>(&shim_nn_swkbd_GetWorkBufSize)},
    {"_ZN2nn5swkbd10MakePresetEPNS0_14KeyboardConfigENS0_6PresetE", reinterpret_cast<void*>(&shim_default_stub)},
    {"_ZN2nn5swkbd16SetGuideTextUtf8EPNS0_14KeyboardConfigEPKc", reinterpret_cast<void*>(&shim_default_stub)},
    {"_ZN2nn5swkbd14SetInitialTextEPNS0_15ShowKeyboardArgEPKDs", reinterpret_cast<void*>(&shim_default_stub)},
    {"_ZN2nn5swkbd12ShowKeyboardEPNS0_6StringERKNS0_15ShowKeyboardArgE", reinterpret_cast<void*>(&shim_nn_swkbd_ShowKeyboard)},
    {"_ZN2nn6detail21UnexpectedDefaultImplEPKcS2_i", reinterpret_cast<void*>(&shim_nn_UnexpectedDefaultImpl)},
    {"__nnmusl_init_dso", reinterpret_cast<void*>(&shim_default_stub)},
    {"__nnmusl_fini_dso", reinterpret_cast<void*>(&shim_default_stub)},
    {"__nnDetailNintendoSdkNsoFile", reinterpret_cast<void*>(&shim_default_stub)},
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

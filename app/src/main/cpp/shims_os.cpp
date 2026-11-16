#include "logger.h"
#include "device_profile.h"
#include "tls_bridge.h"
#include "elf_loader.h"
#include <sys/mman.h>
#include <pthread.h>
#include <unistd.h>
#include <sched.h>
#include <ctime>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cinttypes>
#include <unordered_map>
#include <algorithm>

alignas(64) static uint8_t g_DummyMainThread[512] = {0};
alignas(64) static uint8_t g_DummyDisplay[256] = {0};
alignas(64) static uint8_t g_DummyLayer[256] = {0};
alignas(64) static uint8_t g_DummyNativeWindow[256] = {0};
static const char* g_DummyArgv[] = {"LegoMarvel.nss", nullptr};
static bool g_LoggedHeapReject = false;

extern uint64_t getElfOffsetFromAddr(void* addr);

struct HostMutex {
    pthread_mutex_t mtx;
};

struct HostEvent {
    pthread_mutex_t mtx;
    pthread_cond_t cond;
    bool signaled;
    int clearMode; // 0 = ManualClear, 1 = AutoClear
};

struct HostSemaphore {
    pthread_mutex_t mtx;
    pthread_cond_t cond;
    int count;
    int maxCount;
};

struct HostThread {
    void* threadHandle;
    void (*fn)(void*);
    void* arg;
    int priority;
    pthread_t pt;
    bool started;
};

static pthread_mutex_t g_SyncMapLock = PTHREAD_MUTEX_INITIALIZER;
static std::unordered_map<void*, HostMutex*> g_MutexMap;
static std::unordered_map<void*, HostEvent*> g_EventMap;
static std::unordered_map<void*, HostSemaphore*> g_SemaMap;
static std::unordered_map<void*, HostThread*> g_ThreadMap;

static HostMutex* getOrCreateMutex(void* key, bool recursive = true) {
    pthread_mutex_lock(&g_SyncMapLock);
    auto it = g_MutexMap.find(key);
    if (it != g_MutexMap.end()) {
        pthread_mutex_unlock(&g_SyncMapLock);
        return it->second;
    }
    auto* hm = new HostMutex();
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    if (recursive) {
        pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    }
    pthread_mutex_init(&hm->mtx, &attr);
    pthread_mutexattr_destroy(&attr);
    g_MutexMap[key] = hm;
    pthread_mutex_unlock(&g_SyncMapLock);
    return hm;
}

static HostEvent* getOrCreateEvent(void* key, bool signaled = false, int clearMode = 1) {
    pthread_mutex_lock(&g_SyncMapLock);
    auto it = g_EventMap.find(key);
    if (it != g_EventMap.end()) {
        pthread_mutex_unlock(&g_SyncMapLock);
        return it->second;
    }
    auto* he = new HostEvent();
    pthread_mutex_init(&he->mtx, nullptr);
    pthread_cond_init(&he->cond, nullptr);
    he->signaled = signaled;
    he->clearMode = clearMode;
    g_EventMap[key] = he;
    pthread_mutex_unlock(&g_SyncMapLock);
    return he;
}

static HostSemaphore* getOrCreateSemaphore(void* key, int initialCount = 0, int maxCount = 1) {
    pthread_mutex_lock(&g_SyncMapLock);
    auto it = g_SemaMap.find(key);
    if (it != g_SemaMap.end()) {
        pthread_mutex_unlock(&g_SyncMapLock);
        return it->second;
    }
    auto* hs = new HostSemaphore();
    pthread_mutex_init(&hs->mtx, nullptr);
    pthread_cond_init(&hs->cond, nullptr);
    hs->count = initialCount;
    hs->maxCount = maxCount;
    g_SemaMap[key] = hs;
    pthread_mutex_unlock(&g_SyncMapLock);
    return hs;
}

static void* switchWorkerThreadEntry(void* rawArg) {
    auto* ht = reinterpret_cast<HostThread*>(rawArg);
    uint64_t androidTls = getCurrentTpidrEl0();
    uint8_t* switchTls = allocateThreadSwitchTls(ht->threadHandle);
    bindAndroidTlsToSwitchTls(switchTls, androidTls);

    writeLog("[THREAD] Worker aktif: offset 0x%llx (arg=%p)",
             (unsigned long long)getElfOffsetFromAddr(reinterpret_cast<void*>(ht->fn)), ht->arg);

    setCurrentTpidrEl0(reinterpret_cast<uint64_t>(switchTls));
    if (ht->fn) {
        ht->fn(ht->arg);
    }
    setCurrentTpidrEl0(androidTls);
    return nullptr;
}

extern "C" {

void shim_nn_init_Start() {
    ScopedAndroidTls tls;
    writeLog("[OS] nn::init::Start() dipanggil");
}

uint32_t shim_nn_os_SetMemoryHeapSize(uint64_t requestedSize) {
    ScopedAndroidTls tls;
    uint64_t requestedMB = requestedSize / (1024 * 1024);
    uint64_t limitBytes = g_Device.safeHeapLimitMB * 1024ULL * 1024ULL;

    if (requestedSize > limitBytes) {
        if (!g_LoggedHeapReject) {
            g_LoggedHeapReject = true;
            writeLog("[OS] nn::os::SetMemoryHeapSize(%" PRIu64 " MB) ditolak -> menurunkan ke mode retail", requestedMB);
        }
        return 0x202;
    }

    writeLog("[OS] nn::os::SetMemoryHeapSize(diminta=%" PRIu64 " MB) -> OK", requestedMB);
    return 0;
}

uint32_t shim_nn_os_AllocateMemoryBlock(uintptr_t* outAddress, size_t size) {
    ScopedAndroidTls tls;
    void* ptr = mmap(nullptr, size, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (ptr == MAP_FAILED) {
        writeLog("[OS] Gagal AllocateMemoryBlock(%zu MB)", size / (1024 * 1024));
        return 0x202;
    }
    if (outAddress) *outAddress = reinterpret_cast<uintptr_t>(ptr);
    writeLog("[OS] AllocateMemoryBlock(%zu MB) -> %p", size / (1024 * 1024), ptr);
    return 0;
}

void shim_StandardAllocator_ctor(void* thisPtr) {
    if (thisPtr) memset(thisPtr, 0, 64);
}

void shim_StandardAllocator_Init(void* thisPtr, void* addr, size_t size, bool isCacheEnable) {
    ScopedAndroidTls tls;
    writeLog("[MEM] StandardAllocator::Initialize(addr=%p, size=%zu MB)", addr, size / (1024 * 1024));
}

void* shim_StandardAllocator_Alloc(void* thisPtr, size_t size) {
    ScopedAndroidTls tls;
    return malloc(size > 0 ? size : 16);
}

void* shim_StandardAllocator_AllocAligned(void* thisPtr, size_t size, size_t alignment) {
    ScopedAndroidTls tls;
    if (alignment < sizeof(void*)) alignment = sizeof(void*);
    void* ptr = nullptr;
    if (posix_memalign(&ptr, alignment, size > 0 ? size : alignment) != 0) return nullptr;
    return ptr;
}

void* shim_StandardAllocator_Realloc(void* thisPtr, void* ptr, size_t newSize) {
    ScopedAndroidTls tls;
    return realloc(ptr, newSize);
}

void shim_StandardAllocator_Free(void* thisPtr, void* ptr) {
    ScopedAndroidTls tls;
    free(ptr);
}

void shim_nn_os_InitializeMutex(void* mutexPtr, bool recursive, int lockLevel) {
    ScopedAndroidTls tls;
    if (!mutexPtr) return;
    memset(mutexPtr, 0, 32);
    getOrCreateMutex(mutexPtr, recursive);
}

void shim_nn_os_LockMutex(void* mutexPtr) {
    ScopedAndroidTls tls;
    if (!mutexPtr) return;
    pthread_mutex_lock(&getOrCreateMutex(mutexPtr, true)->mtx);
}

bool shim_nn_os_TryLockMutex(void* mutexPtr) {
    ScopedAndroidTls tls;
    if (!mutexPtr) return true;
    return pthread_mutex_trylock(&getOrCreateMutex(mutexPtr, true)->mtx) == 0;
}

void shim_nn_os_UnlockMutex(void* mutexPtr) {
    ScopedAndroidTls tls;
    if (!mutexPtr) return;
    pthread_mutex_unlock(&getOrCreateMutex(mutexPtr, true)->mtx);
}

void shim_nn_os_FinalizeMutex(void* mutexPtr) {}

void shim_nn_os_InitializeEvent(void* eventPtr, bool signaled, int clearMode) {
    ScopedAndroidTls tls;
    if (!eventPtr) return;
    memset(eventPtr, 0, 64);
    HostEvent* he = getOrCreateEvent(eventPtr, signaled, clearMode);
    pthread_mutex_lock(&he->mtx);
    he->signaled = signaled;
    he->clearMode = clearMode;
    pthread_mutex_unlock(&he->mtx);
}

void shim_nn_os_WaitEvent(void* eventPtr) {
    ScopedAndroidTls tls;
    if (!eventPtr) return;
    HostEvent* he = getOrCreateEvent(eventPtr);
    pthread_mutex_lock(&he->mtx);
    while (!he->signaled) {
        pthread_cond_wait(&he->cond, &he->mtx);
    }
    if (he->clearMode == 1) {
        he->signaled = false;
    }
    pthread_mutex_unlock(&he->mtx);
}

void shim_nn_os_SignalEvent(void* eventPtr) {
    ScopedAndroidTls tls;
    if (!eventPtr) return;
    HostEvent* he = getOrCreateEvent(eventPtr);
    pthread_mutex_lock(&he->mtx);
    he->signaled = true;
    pthread_cond_broadcast(&he->cond);
    pthread_mutex_unlock(&he->mtx);
}

void shim_nn_os_ClearEvent(void* eventPtr) {
    ScopedAndroidTls tls;
    if (!eventPtr) return;
    HostEvent* he = getOrCreateEvent(eventPtr);
    pthread_mutex_lock(&he->mtx);
    he->signaled = false;
    pthread_mutex_unlock(&he->mtx);
}

void shim_nn_os_FinalizeEvent(void* eventPtr) {}

void shim_nn_os_InitializeSemaphore(void* semaPtr, int initialCount, int maxCount) {
    ScopedAndroidTls tls;
    if (!semaPtr) return;
    memset(semaPtr, 0, 64);
    HostSemaphore* hs = getOrCreateSemaphore(semaPtr, initialCount, maxCount);
    pthread_mutex_lock(&hs->mtx);
    hs->count = initialCount;
    hs->maxCount = maxCount;
    pthread_mutex_unlock(&hs->mtx);
}

void shim_nn_os_AcquireSemaphore(void* semaPtr) {
    ScopedAndroidTls tls;
    if (!semaPtr) return;
    HostSemaphore* hs = getOrCreateSemaphore(semaPtr);
    pthread_mutex_lock(&hs->mtx);
    while (hs->count <= 0) {
        pthread_cond_wait(&hs->cond, &hs->mtx);
    }
    hs->count--;
    pthread_mutex_unlock(&hs->mtx);
}

bool shim_nn_os_TryAcquireSemaphore(void* semaPtr) {
    ScopedAndroidTls tls;
    if (!semaPtr) return true;
    HostSemaphore* hs = getOrCreateSemaphore(semaPtr);
    pthread_mutex_lock(&hs->mtx);
    bool ok = false;
    if (hs->count > 0) {
        hs->count--;
        ok = true;
    }
    pthread_mutex_unlock(&hs->mtx);
    return ok;
}

void shim_nn_os_ReleaseSemaphore(void* semaPtr, int count) {
    ScopedAndroidTls tls;
    if (!semaPtr) return;
    HostSemaphore* hs = getOrCreateSemaphore(semaPtr);
    pthread_mutex_lock(&hs->mtx);
    hs->count += count;
    pthread_cond_broadcast(&hs->cond);
    pthread_mutex_unlock(&hs->mtx);
}

void shim_nn_os_FinalizeSemaphore(void* semaPtr) {}

uint32_t shim_nn_os_CreateThread(void* thread, void* fn, void* arg, void* stack, size_t stackSize, int priority) {
    ScopedAndroidTls tls;
    if (thread) memset(thread, 0, 256);
    auto* ht = new HostThread{thread, reinterpret_cast<void (*)(void*)>(fn), arg, priority, 0, false};
    pthread_mutex_lock(&g_SyncMapLock);
    g_ThreadMap[thread] = ht;
    pthread_mutex_unlock(&g_SyncMapLock);

    writeLog("[OS] nn::os::CreateThread(fn=0x%llx, stack=%zu KB, prio=%d)",
             (unsigned long long)getElfOffsetFromAddr(fn), stackSize / 1024, priority);
    return 0;
}

uint32_t shim_nn_os_CreateThreadCore(void* thread, void* fn, void* arg, void* stack, size_t stackSize, int priority, int core) {
    return shim_nn_os_CreateThread(thread, fn, arg, stack, stackSize, priority);
}

void shim_nn_os_StartThread(void* thread) {
    ScopedAndroidTls tls;
    pthread_mutex_lock(&g_SyncMapLock);
    auto it = g_ThreadMap.find(thread);
    HostThread* ht = (it != g_ThreadMap.end()) ? it->second : nullptr;
    pthread_mutex_unlock(&g_SyncMapLock);

    if (ht && !ht->started) {
        ht->started = true;
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setstacksize(&attr, 2 * 1024 * 1024);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        pthread_create(&ht->pt, &attr, switchWorkerThreadEntry, ht);
        pthread_attr_destroy(&attr);
    }
    writeLog("[OS] nn::os::StartThread(%p)", thread);
}

void* shim_nn_os_GetCurrentThread() {
    void* h = getThreadSwitchHandle();
    return h ? h : g_DummyMainThread;
}

int shim_nn_os_GetHostArgc() {
    return 1;
}

const char** shim_nn_os_GetHostArgv() {
    return g_DummyArgv;
}

void shim_nn_os_SleepThread(int64_t nanoSeconds) {
    if (nanoSeconds > 0) {
        useconds_t us = static_cast<useconds_t>(std::min<int64_t>(nanoSeconds / 1000, 16000));
        if (us > 0) usleep(us);
    }
}

void shim_nn_os_YieldThread() {
    sched_yield();
}

int64_t shim_nn_os_GetSystemTick() {
    struct timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    int64_t ns = static_cast<int64_t>(ts.tv_sec) * 1000000000LL + ts.tv_nsec;
    return (ns * 192) / 10000;
}

int64_t shim_nn_os_GetSystemTickFrequency() {
    return 19200000LL;
}

uint32_t shim_nn_time_Initialize() {
    ScopedAndroidTls tls;
    writeLog("[TIME] nn::time::Initialize()");
    return 0;
}

uint32_t shim_nn_time_GetCurrentTime(int64_t* outPosixTime) {
    if (outPosixTime) *outPosixTime = static_cast<int64_t>(time(nullptr));
    return 0;
}

void shim_nn_vi_Initialize() {
    ScopedAndroidTls tls;
    writeLog("[VI] nn::vi::Initialize()");
}

uint32_t shim_nn_vi_OpenDefaultDisplay(void** outDisplay) {
    ScopedAndroidTls tls;
    if (outDisplay) *outDisplay = g_DummyDisplay;
    writeLog("[VI] nn::vi::OpenDefaultDisplay() -> %p", g_DummyDisplay);
    return 0;
}

uint32_t shim_nn_vi_CreateLayer(void** outLayer, void* display, int width, int height) {
    ScopedAndroidTls tls;
    if (outLayer) *outLayer = g_DummyLayer;
    writeLog("[VI] nn::vi::CreateLayer(%dx%d) -> %p", width, height, g_DummyLayer);
    return 0;
}

uint32_t shim_nn_vi_GetNativeWindow(void** outWindow, void* layer) {
    ScopedAndroidTls tls;
    if (outWindow) *outWindow = g_DummyNativeWindow;
    writeLog("[VI] nn::vi::GetNativeWindow() -> %p", g_DummyNativeWindow);
    return 0;
}

int shim_nn_oe_GetOperationMode() {
    return 0;
}

int shim_nn_oe_GetPerformanceMode() {
    return 0;
}

int shim_nn_oe_GetCurrentFocusState() {
    return 1;
}

uint32_t shim_nn_oe_PopNotificationMessage() {
    usleep(16000); // Cegah HandleMessageAutoLoop busy-spin 100% CPU
    return 0;
}

uint64_t shim_nn_oe_GetDesiredLanguage() {
    uint64_t code = 0;
    memcpy(&code, "en-US", 5);
    return code;
}

uint64_t shim_nn_settings_LanguageCode_Make(int language) {
    uint64_t code = 0;
    memcpy(&code, "en-US", 5);
    return code;
}

void shim_nn_account_Initialize() {
    ScopedAndroidTls tls;
    writeLog("[ACCOUNT] nn::account::Initialize()");
}

bool shim_nn_account_TryOpenPreselectedUser(void* outUserHandle) {
    ScopedAndroidTls tls;
    if (outUserHandle) {
        memset(outUserHandle, 0, 16);
        *reinterpret_cast<uint64_t*>(outUserHandle) = 1ULL;
    }
    writeLog("[ACCOUNT] nn::account::TryOpenPreselectedUser() -> TRUE");
    return true;
}

uint32_t shim_nn_account_GetUserId(void* outUid, const void* userHandle) {
    if (outUid) {
        uint64_t* uid = reinterpret_cast<uint64_t*>(outUid);
        uid[0] = 0x1111222233334444ULL;
        uid[1] = 0x5555666677778888ULL;
    }
    return 0;
}

void shim_nn_hid_InitializeNpad() {
    ScopedAndroidTls tls;
    writeLog("[HID] nn::hid::InitializeNpad()");
}

uint32_t shim_nn_hid_GetNpadStyleSet(const uint32_t* npadId) {
    return 0x2;
}

void shim_nn_fs_SetAllocator(void* allocFn, void* deallocFn) {
    ScopedAndroidTls tls;
    writeLog("[FS] nn::fs::SetAllocator dipanggil");
}

void shim_nv_SetGraphicsAllocator(void* allocFn, void* freeFn, void* reallocFn, void* userData) {
    ScopedAndroidTls tls;
    writeLog("[NV] nv::SetGraphicsAllocator dipanggil");
}

uint32_t shim_nv_InitializeGraphics(void* addr, size_t size) {
    ScopedAndroidTls tls;
    writeLog("[NV] nv::InitializeGraphics(addr=%p, size=%.2f MB)", addr, size / (1024.0 * 1024.0));
    return 0;
}

void shim_movie_SetAllocator(void* allocFn, void* freeFn, void* reallocFn, void* userData) {
    ScopedAndroidTls tls;
    writeLog("[MOVIE] movie::SetAllocator dipanggil");
}

}

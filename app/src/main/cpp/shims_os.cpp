#include "logger.h"
#include "device_profile.h"
#include "tls_bridge.h"
#include <sys/mman.h>
#include <unistd.h>
#include <sched.h>
#include <ctime>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cinttypes>
#include <algorithm>

alignas(64) static uint8_t g_DummyMainThread[512] = {0};
static const char* g_DummyArgv[] = {"LegoMarvel.nss", nullptr};
static bool g_LoggedHeapReject = false;

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
    if (mutexPtr) memset(mutexPtr, 0, 32);
}

void shim_nn_os_LockMutex(void* mutexPtr) {}
void shim_nn_os_UnlockMutex(void* mutexPtr) {}

void shim_nn_os_InitializeEvent(void* eventPtr, bool signaled, int clearMode) {
    if (eventPtr) memset(eventPtr, 0, 64);
}
void shim_nn_os_WaitEvent(void* eventPtr) {}
void shim_nn_os_SignalEvent(void* eventPtr) {}
void shim_nn_os_ClearEvent(void* eventPtr) {}
void shim_nn_os_FinalizeEvent(void* eventPtr) {}

void shim_nn_os_InitializeSemaphore(void* semaPtr, int initialCount, int maxCount) {
    if (semaPtr) memset(semaPtr, 0, 64);
}
void shim_nn_os_AcquireSemaphore(void* semaPtr) {}
bool shim_nn_os_TryAcquireSemaphore(void* semaPtr) { return true; }
void shim_nn_os_ReleaseSemaphore(void* semaPtr, int count) {}
void shim_nn_os_FinalizeSemaphore(void* semaPtr) {}

void* shim_nn_os_GetCurrentThread() {
    return g_DummyMainThread;
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

void shim_nn_fs_SetAllocator(void* allocFn, void* deallocFn) {
    ScopedAndroidTls tls;
    writeLog("[FS] nn::fs::SetAllocator dipanggil");
}

void shim_nv_SetGraphicsAllocator(void* allocFn, void* freeFn, void* reallocFn, void* userData) {
    ScopedAndroidTls tls;
    writeLog("[NV] nv::SetGraphicsAllocator dipanggil");
}

void shim_movie_SetAllocator(void* allocFn, void* freeFn, void* reallocFn, void* userData) {
    ScopedAndroidTls tls;
    writeLog("[MOVIE] movie::SetAllocator dipanggil");
}

}

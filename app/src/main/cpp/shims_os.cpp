#include "logger.h"
#include "device_profile.h"
#include <sys/mman.h>
#include <ctime>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cinttypes>
#include <algorithm>

static alignas(64) uint8_t g_DummyMainThread[512] = {0};

extern "C" {

void shim_nn_init_Start() {
    writeLog("[OS] nn::init::Start() dipanggil");
}

uint32_t shim_nn_os_SetMemoryHeapSize(uint64_t requestedSize) {
    uint64_t requestedMB = requestedSize / (1024 * 1024);
    uint64_t grantedMB = std::min(requestedMB, g_Device.safeHeapLimitMB);
    writeLog("[OS] nn::os::SetMemoryHeapSize(diminta=%" PRIu64 " MB -> dialokasikan=%" PRIu64 " MB)",
             requestedMB, grantedMB);
    return 0;
}

uint32_t shim_nn_os_AllocateMemoryBlock(uintptr_t* outAddress, size_t size) {
    void* ptr = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (ptr == MAP_FAILED) {
        writeLog("[OS] Gagal AllocateMemoryBlock(%zu bytes)", size);
        return 1;
    }
    if (outAddress) *outAddress = reinterpret_cast<uintptr_t>(ptr);
    writeLog("[OS] AllocateMemoryBlock(%zu bytes) -> %p", size, ptr);
    return 0;
}

void shim_StandardAllocator_ctor(void* thisPtr) {
    if (thisPtr) memset(thisPtr, 0, 64);
}

void shim_StandardAllocator_Init(void* thisPtr, void* addr, size_t size, bool isCacheEnable) {
    writeLog("[MEM] StandardAllocator::Initialize(addr=%p, size=%zu)", addr, size);
}

void* shim_StandardAllocator_Alloc(void* thisPtr, size_t size) {
    return malloc(size > 0 ? size : 16);
}

void* shim_StandardAllocator_AllocAligned(void* thisPtr, size_t size, size_t alignment) {
    if (alignment < sizeof(void*)) alignment = sizeof(void*);
    void* ptr = nullptr;
    if (posix_memalign(&ptr, alignment, size > 0 ? size : alignment) != 0) return nullptr;
    return ptr;
}

void* shim_StandardAllocator_Realloc(void* thisPtr, void* ptr, size_t newSize) {
    return realloc(ptr, newSize);
}

void shim_StandardAllocator_Free(void* thisPtr, void* ptr) {
    free(ptr);
}

void shim_nn_os_InitializeMutex(void* mutexPtr, bool recursive, int lockLevel) {
    if (mutexPtr) memset(mutexPtr, 0, 32);
}

void shim_nn_os_LockMutex(void* mutexPtr) {}
void shim_nn_os_UnlockMutex(void* mutexPtr) {}

void* shim_nn_os_GetCurrentThread() {
    return g_DummyMainThread;
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

}

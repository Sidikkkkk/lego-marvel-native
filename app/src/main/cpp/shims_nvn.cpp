#include "logger.h"
#include "device_profile.h"
#include "tls_bridge.h"
#include <sys/mman.h>
#include <ctime>
#include <cstring>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <string>

struct NvnPoolState {
    void* memory;
    size_t size;
};

struct NvnBufferState {
    NvnPoolState* pool;
    int64_t offset;
    size_t size;
};

static std::unordered_map<void*, NvnPoolState> g_PoolBuilders;
static std::unordered_map<void*, NvnPoolState> g_Pools;
static std::unordered_map<void*, NvnBufferState> g_BufferBuilders;
static std::unordered_map<void*, NvnBufferState> g_Buffers;
static std::unordered_set<std::string> g_LoggedNvnProcs;

extern "C" {

int shim_nvn_default_ok() {
    return 1;
}

int shim_nvnDeviceInitialize(void* device, const void* builder) {
    ScopedAndroidTls tls;
    if (device) memset(device, 0, 256);
    writeLog("[NVN] nvnDeviceInitialize(%p) -> TRUE", device);
    return 1;
}

void shim_nvnDeviceGetInteger(const void* device, int pname, int* v) {
    if (!v) return;
    // Nilai properti GPU Switch yang aman agar engine tidak mengalami pembagian nol / alignment error
    switch (pname) {
        case 0:  *v = 1; break;      // API major version
        case 1:  *v = 53; break;     // API minor version
        case 2:  *v = 256; break;    // Uniform buffer alignment
        case 3:  *v = 16384; break;  // Max texture size
        case 4:  *v = 2048; break;   // Max 3D texture size
        case 5:  *v = 16384; break;  // Max cube map texture size
        case 6:  *v = 16384; break;  // Max rectangle texture size
        case 7:  *v = 2048; break;   // Max texture array layers
        case 8:  *v = 8; break;      // Max color attachments
        case 9:  *v = 16; break;     // Max vertex attribs
        case 10: *v = 16; break;     // Max vertex buffers
        case 11: *v = 4096; break;   // Max vertex buffer stride
        case 12: *v = 256; break;    // Texture buffer alignment
        case 13: *v = 256; break;    // Storage buffer alignment
        case 14: *v = 65536; break;  // Max uniform buffer size
        default: *v = 256; break;
    }
}

uint64_t shim_nvnDeviceGetCurrentTimestampInNanoseconds(const void* device) {
    struct timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000000000ULL + static_cast<uint64_t>(ts.tv_nsec);
}

void shim_nvnMemoryPoolBuilderSetDefaults(void* builder) {
    if (builder) memset(builder, 0, 128);
    g_PoolBuilders[builder] = {nullptr, 0};
}

void shim_nvnMemoryPoolBuilderSetStorage(void* builder, void* memory, size_t size) {
    g_PoolBuilders[builder] = {memory, size};
}

int shim_nvnMemoryPoolInitialize(void* pool, void* builder) {
    ScopedAndroidTls tls;
    if (pool) memset(pool, 0, 256);
    auto it = g_PoolBuilders.find(builder);
    if (it != g_PoolBuilders.end() && it->second.memory != nullptr) {
        g_Pools[pool] = it->second;
    } else {
        void* fallback = mmap(nullptr, 64 * 1024 * 1024, PROT_READ | PROT_WRITE,
                              MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
        g_Pools[pool] = {fallback, 64 * 1024 * 1024};
    }
    return 1;
}

void* shim_nvnMemoryPoolGetBufferAddress(void* pool) {
    auto it = g_Pools.find(pool);
    if (it != g_Pools.end() && it->second.memory != nullptr) {
        return it->second.memory;
    }
    return nullptr;
}

uint64_t shim_nvnMemoryPoolGetSize(void* pool) {
    auto it = g_Pools.find(pool);
    return (it != g_Pools.end()) ? it->second.size : (64ULL * 1024ULL * 1024ULL);
}

void shim_nvnBufferBuilderSetDefaults(void* builder) {
    if (builder) memset(builder, 0, 128);
    g_BufferBuilders[builder] = {nullptr, 0, 0};
}

void shim_nvnBufferBuilderSetStorage(void* builder, void* pool, int64_t offset, size_t size) {
    auto it = g_Pools.find(pool);
    NvnPoolState* pState = (it != g_Pools.end()) ? &it->second : nullptr;
    g_BufferBuilders[builder] = {pState, offset, size};
}

int shim_nvnBufferInitialize(void* buffer, void* builder) {
    if (buffer) memset(buffer, 0, 128);
    auto it = g_BufferBuilders.find(builder);
    if (it != g_BufferBuilders.end()) {
        g_Buffers[buffer] = it->second;
    }
    return 1;
}

uint64_t shim_nvnBufferGetAddress(void* buffer) {
    auto it = g_Buffers.find(buffer);
    if (it != g_Buffers.end() && it->second.pool && it->second.pool->memory) {
        return reinterpret_cast<uint64_t>(it->second.pool->memory) + static_cast<uint64_t>(it->second.offset);
    }
    return reinterpret_cast<uint64_t>(buffer);
}

void* shim_nvnBufferMap(void* buffer) {
    return reinterpret_cast<void*>(shim_nvnBufferGetAddress(buffer));
}

int shim_nvnWindowAcquireTexture(void* window, void* textureAvailableSync, int* textureIndex) {
    if (textureIndex) *textureIndex = 0;
    return 0; // NVN_WINDOW_ACQUIRE_TEXTURE_RESULT_SUCCESS
}

int shim_nvnSyncWait(const void* sync, uint64_t timeoutNs) {
    return 1; // NVN_SYNC_WAIT_RESULT_ALREADY_SIGNALED
}

uint64_t shim_nvnCommandBufferEndRecording(void* cmdBuf) {
    return 0x1000ULL;
}

void* shim_nvnDeviceGetProcAddress(void* device, const char* name) {
    ScopedAndroidTls tls;
    if (!name || name[0] == '\0') {
        return reinterpret_cast<void*>(&shim_nvn_default_ok);
    }

    if (g_LoggedNvnProcs.insert(name).second) {
        writeLog("[NVN-PROC] %s", name);
    }

    if (strcmp(name, "nvnDeviceGetProcAddress") == 0) return reinterpret_cast<void*>(&shim_nvnDeviceGetProcAddress);
    if (strcmp(name, "nvnDeviceInitialize") == 0) return reinterpret_cast<void*>(&shim_nvnDeviceInitialize);
    if (strcmp(name, "nvnDeviceGetInteger") == 0) return reinterpret_cast<void*>(&shim_nvnDeviceGetInteger);
    if (strcmp(name, "nvnDeviceGetCurrentTimestampInNanoseconds") == 0) return reinterpret_cast<void*>(&shim_nvnDeviceGetCurrentTimestampInNanoseconds);
    if (strcmp(name, "nvnMemoryPoolBuilderSetDefaults") == 0) return reinterpret_cast<void*>(&shim_nvnMemoryPoolBuilderSetDefaults);
    if (strcmp(name, "nvnMemoryPoolBuilderSetStorage") == 0) return reinterpret_cast<void*>(&shim_nvnMemoryPoolBuilderSetStorage);
    if (strcmp(name, "nvnMemoryPoolInitialize") == 0) return reinterpret_cast<void*>(&shim_nvnMemoryPoolInitialize);
    if (strcmp(name, "nvnMemoryPoolGetBufferAddress") == 0) return reinterpret_cast<void*>(&shim_nvnMemoryPoolGetBufferAddress);
    if (strcmp(name, "nvnMemoryPoolGetSize") == 0) return reinterpret_cast<void*>(&shim_nvnMemoryPoolGetSize);
    if (strcmp(name, "nvnBufferBuilderSetDefaults") == 0) return reinterpret_cast<void*>(&shim_nvnBufferBuilderSetDefaults);
    if (strcmp(name, "nvnBufferBuilderSetStorage") == 0) return reinterpret_cast<void*>(&shim_nvnBufferBuilderSetStorage);
    if (strcmp(name, "nvnBufferInitialize") == 0) return reinterpret_cast<void*>(&shim_nvnBufferInitialize);
    if (strcmp(name, "nvnBufferGetAddress") == 0) return reinterpret_cast<void*>(&shim_nvnBufferGetAddress);
    if (strcmp(name, "nvnBufferMap") == 0) return reinterpret_cast<void*>(&shim_nvnBufferMap);
    if (strcmp(name, "nvnWindowAcquireTexture") == 0) return reinterpret_cast<void*>(&shim_nvnWindowAcquireTexture);
    if (strcmp(name, "nvnSyncWait") == 0) return reinterpret_cast<void*>(&shim_nvnSyncWait);
    if (strcmp(name, "nvnCommandBufferEndRecording") == 0) return reinterpret_cast<void*>(&shim_nvnCommandBufferEndRecording);

    return reinterpret_cast<void*>(&shim_nvn_default_ok);
}

void* shim_nvnBootstrapLoader(const char* procName) {
    ScopedAndroidTls tls;
    writeLog("[NVN] nvnBootstrapLoader mencari fungsi: %s", procName ? procName : "(null)");
    return shim_nvnDeviceGetProcAddress(nullptr, procName);
}

}

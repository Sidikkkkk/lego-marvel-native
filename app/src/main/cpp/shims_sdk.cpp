#include "logger.h"
#include "tls_bridge.h"
#include <malloc.h>
#include <ctime>
#include <cstring>
#include <cstdint>
#include <atomic>
#include <unordered_map>
#include <vector>

std::atomic<uint64_t> g_TouchSwitchButtons{0};
static std::atomic<int64_t> g_NpadSamplingCounter{1};
static void* g_AudioSystemEvent = nullptr;
static std::unordered_map<void*, std::vector<const void*>> g_VoiceWaveQueue;

extern "C" {
    void shim_nn_os_SignalEvent(void* eventPtr);
    uint32_t shim_nn_fs_DeleteFile(const char* path);
}

extern "C" {

// --- StandardAllocator::GetSizeOf ---
size_t shim_StandardAllocator_GetSizeOf(const void* thisPtr, const void* ptr) {
    ScopedAndroidTls tls;
    if (!ptr) return 0;
    uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
    if ((addr & 0xFFF) == 0 && addr >= 0x7000000000ULL) {
        return 64 * 1024 * 1024;
    }
    return malloc_usable_size(const_cast<void*>(ptr));
}

// --- nn::util::BinaryFileHeader & RelocationTable (BNSH / BNTX / Shader) ---
bool shim_BinaryFileHeader_IsValid(const void* thisPtr, int64_t expectedMagic, int majorVer, int minorVer, int microVer) {
    if (!thisPtr) return false;
    uint32_t magic32 = *reinterpret_cast<const uint32_t*>(thisPtr);
    return magic32 != 0;
}

bool shim_BinaryFileHeader_IsRelocated(const void* thisPtr) {
    if (!thisPtr) return true;
    const uint8_t* base = reinterpret_cast<const uint8_t*>(thisPtr);
    uint16_t flags = *reinterpret_cast<const uint16_t*>(base + 0x14);
    return (flags & 1) != 0;
}

void* shim_BinaryFileHeader_GetFirstBlock(const void* thisPtr) {
    if (!thisPtr) return nullptr;
    const uint8_t* base = reinterpret_cast<const uint8_t*>(thisPtr);
    uint16_t firstBlockOff = *reinterpret_cast<const uint16_t*>(base + 0x16);
    if (firstBlockOff == 0) return nullptr;
    return const_cast<uint8_t*>(base + firstBlockOff);
}

void* shim_BinaryFileHeader_GetRelocationTable(const void* thisPtr) {
    if (!thisPtr) return nullptr;
    const uint8_t* base = reinterpret_cast<const uint8_t*>(thisPtr);
    uint32_t rltOff = *reinterpret_cast<const uint32_t*>(base + 0x18);
    if (rltOff == 0) return nullptr;
    return const_cast<uint8_t*>(base + rltOff);
}

void shim_RelocationTable_Relocate(void* rltPtr) {
    if (!rltPtr) return;
    uint8_t* rlt = reinterpret_cast<uint8_t*>(rltPtr);
    if (memcmp(rlt, "_RLT", 4) != 0) return;

    uint32_t rltPos = *reinterpret_cast<uint32_t*>(rlt + 4);
    int32_t sectionCount = *reinterpret_cast<int32_t*>(rlt + 8);
    if (sectionCount <= 0 || sectionCount > 64) return;

    uint8_t* fileBase = rlt - rltPos;
    uint16_t* headerFlags = reinterpret_cast<uint16_t*>(fileBase + 0x14);
    if ((*headerFlags & 1) != 0) return;

    struct RltSection {
        uint64_t basePtr;
        uint32_t position;
        uint32_t size;
        int32_t entryIndex;
        int32_t entryCount;
    };
    struct RltEntry {
        uint32_t position;
        uint16_t structCount;
        uint8_t offsetCount;
        uint8_t paddingCount;
    };

    auto* sections = reinterpret_cast<RltSection*>(rlt + 16);
    auto* entries = reinterpret_cast<RltEntry*>(rlt + 16 + sectionCount * sizeof(RltSection));

    for (int32_t s = 0; s < sectionCount; ++s) {
        uint8_t* secBase = (sections[s].basePtr != 0)
                           ? reinterpret_cast<uint8_t*>(sections[s].basePtr)
                           : (fileBase + sections[s].position);
        for (int32_t e = 0; e < sections[s].entryCount; ++e) {
            const RltEntry& ent = entries[sections[s].entryIndex + e];
            uint8_t* cur = fileBase + ent.position;
            size_t stride = (static_cast<size_t>(ent.offsetCount) + static_cast<size_t>(ent.paddingCount)) * 8;
            for (uint16_t st = 0; st < ent.structCount; ++st) {
                uint64_t* ptrs = reinterpret_cast<uint64_t*>(cur + st * stride);
                for (uint8_t o = 0; o < ent.offsetCount; ++o) {
                    if (ptrs[o] != 0) {
                        ptrs[o] += reinterpret_cast<uint64_t>(secBase);
                    }
                }
            }
        }
    }
    *headerFlags |= 1;
}

void* shim_nn_util_ReferSymbol(const void* sym) {
    return const_cast<void*>(sym);
}

// --- nn::err (Menangkap Pesan Error Internal Game) ---
struct GameErrorArg {
    uint32_t errorCode;
    char dialogMsg[512];
    char fullMsg[512];
};

void shim_ApplicationErrorArg_ctor(void* thisPtr) {
    if (thisPtr) memset(thisPtr, 0, sizeof(GameErrorArg));
}

void shim_ApplicationErrorArg_SetCode(void* thisPtr, uint32_t code) {
    if (thisPtr) reinterpret_cast<GameErrorArg*>(thisPtr)->errorCode = code;
}

void shim_ApplicationErrorArg_SetDialogMsg(void* thisPtr, const char* msg) {
    if (thisPtr && msg) {
        strncpy(reinterpret_cast<GameErrorArg*>(thisPtr)->dialogMsg, msg, 511);
    }
}

void shim_ApplicationErrorArg_SetFullMsg(void* thisPtr, const char* msg) {
    if (thisPtr && msg) {
        strncpy(reinterpret_cast<GameErrorArg*>(thisPtr)->fullMsg, msg, 511);
    }
}

void shim_ApplicationErrorArg_SetLang(void* thisPtr, const void* lang) {}

void shim_nn_err_ShowApplicationError(const void* argPtr) {
    ScopedAndroidTls tls;
    if (argPtr) {
        const auto* arg = reinterpret_cast<const GameErrorArg*>(argPtr);
        writeLog("[GAME-ERR] ShowApplicationError(code=0x%x, dialog='%s', full='%s')",
                 arg->errorCode, arg->dialogMsg, arg->fullMsg);
    }
}

// --- nn::audio (Audio Renderer + Auto Buffer Release) ---
void shim_nn_audio_InitParam(void* param) {
    if (param) memset(param, 0, 64);
}

size_t shim_nn_audio_GetConfigWorkBufSize(const void* param) {
    return 64 * 1024;
}

size_t shim_nn_audio_GetRendererWorkBufSize(const void* param) {
    return 64 * 1024;
}

bool shim_nn_audio_IsValidParam(const void* param) {
    return true;
}

void shim_nn_audio_InitConfig(void* config, const void* param, void* workBuf, size_t workBufSize) {
    if (config) memset(config, 0, 256);
}

uint32_t shim_nn_audio_OpenRenderer(void* handle, void* sysEvent, const void* param, void* workBuf, size_t workBufSize) {
    ScopedAndroidTls tls;
    if (handle) *reinterpret_cast<uint64_t*>(handle) = 1ULL;
    g_AudioSystemEvent = sysEvent;
    if (sysEvent) {
        shim_nn_os_SignalEvent(sysEvent);
        shim_nn_os_SignalEvent(reinterpret_cast<uint8_t*>(sysEvent) + 8);
    }
    writeLog("[AUDIO] nn::audio::OpenAudioRenderer() -> OK");
    return 0;
}

uint32_t shim_nn_audio_StartRenderer(void* handle) {
    ScopedAndroidTls tls;
    writeLog("[AUDIO] nn::audio::StartAudioRenderer() -> OK");
    return 0;
}

uint32_t shim_nn_audio_RequestUpdateRenderer(void* handle, const void* config) {
    if (g_AudioSystemEvent) {
        shim_nn_os_SignalEvent(g_AudioSystemEvent);
        shim_nn_os_SignalEvent(reinterpret_cast<uint8_t*>(g_AudioSystemEvent) + 8);
    }
    return 0;
}

bool shim_nn_audio_AcquireTrue() { return true; }

bool shim_nn_audio_AppendWaveBuffer(void* voice, const void* waveBuf) {
    ScopedAndroidTls tls;
    if (voice && waveBuf) {
        g_VoiceWaveQueue[voice].push_back(waveBuf);
    }
    return true;
}

const void* shim_nn_audio_GetReleasedWaveBuffer(void* voice) {
    ScopedAndroidTls tls;
    auto it = g_VoiceWaveQueue.find(voice);
    if (it != g_VoiceWaveQueue.end() && !it->second.empty()) {
        const void* front = it->second.front();
        it->second.erase(it->second.begin());
        return front;
    }
    return nullptr;
}

int shim_nn_audio_GetWaveBufferCount(const void* voice) {
    ScopedAndroidTls tls;
    auto it = g_VoiceWaveQueue.find(const_cast<void*>(voice));
    return (it != g_VoiceWaveQueue.end()) ? static_cast<int>(it->second.size()) : 0;
}

int shim_nn_audio_GetVoicePlayState(const void* voice) {
    return 0; // PlayState_Play
}

// --- nn::hid (Controller + Android Touch Bridge) ---
void shim_nn_hid_GetNpadState(void* outState, const uint32_t* npadId) {
    if (!outState) return;
    memset(outState, 0, 64);
    int64_t* words = reinterpret_cast<int64_t*>(outState);
    words[0] = g_NpadSamplingCounter.fetch_add(1);
    words[1] = static_cast<int64_t>(g_TouchSwitchButtons.load());
    // offset 0x20: attributes (bit 0 = IsConnected)
    *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(outState) + 0x20) = 1;
}

int shim_nn_hid_GetHandles(void* outHandles, int count, const uint32_t* npadId, uint32_t styleSet) {
    if (outHandles && count > 0) {
        memset(outHandles, 0, static_cast<size_t>(count) * 8);
    }
    return (count > 0) ? 1 : 0;
}

uint32_t shim_nn_hid_ShowControllerSupport(const void* arg) {
    return 0;
}

// --- glslc (Shader Compiler Stubs) ---
uint8_t shim_glslc_Initialize() { return 1; }
void shim_glslc_noop() {}

// --- nn::socket (Offline Network Stubs) ---
int shim_nn_socket_Error() { return -1; }
int shim_nn_socket_Ok() { return 0; }
uint16_t shim_nn_socket_InetNtohs(uint16_t netshort) { return __builtin_bswap16(netshort); }

// --- nn::swkbd (Software Keyboard Stubs) ---
size_t shim_nn_swkbd_GetStringBufSize() { return 2048; }
size_t shim_nn_swkbd_GetWorkBufSize(bool enableDic) { return 16384; }
uint32_t shim_nn_swkbd_ShowKeyboard(void* outString, const void* arg) {
    if (outString) {
        char** strPtr = reinterpret_cast<char**>(outString);
        if (*strPtr) strcpy(*strPtr, "Player1");
    }
    return 0;
}

// --- nn::time & nn::settings & nn::fs extras ---
uint32_t shim_nn_time_ToCalendarTimeUtc(void* outCal, const int64_t* posixTime) {
    ScopedAndroidTls tls;
    if (!outCal) return 0x202;
    time_t t = posixTime ? static_cast<time_t>(*posixTime) : time(nullptr);
    struct tm tmVal{};
    gmtime_r(&t, &tmVal);
    int16_t* year = reinterpret_cast<int16_t*>(outCal);
    int8_t* fields = reinterpret_cast<int8_t*>(outCal);
    *year = static_cast<int16_t>(tmVal.tm_year + 1900);
    fields[2] = static_cast<int8_t>(tmVal.tm_mon + 1);
    fields[3] = static_cast<int8_t>(tmVal.tm_mday);
    fields[4] = static_cast<int8_t>(tmVal.tm_hour);
    fields[5] = static_cast<int8_t>(tmVal.tm_min);
    fields[6] = static_cast<int8_t>(tmVal.tm_sec);
    return 0;
}

uint32_t shim_nn_time_ToCalendarTimeLocal(void* outCal, void* outAddInfo, const int64_t* posixTime) {
    if (outAddInfo) memset(outAddInfo, 0, 24);
    return shim_nn_time_ToCalendarTimeUtc(outCal, posixTime);
}

bool shim_nn_settings_LanguageEq(const uint64_t* a, const uint64_t* b) {
    if (!a || !b) return false;
    return (*a == *b);
}

uint32_t shim_nn_fs_RenameFile(const char* oldPath, const char* newPath) {
    return 0;
}

uint32_t shim_nn_fs_DeleteDirRecursive(const char* path) {
    return shim_nn_fs_DeleteFile(path);
}

void shim_nn_UnexpectedDefaultImpl(const char* func, const char* file, int line) {
    ScopedAndroidTls tls;
    writeLog("[WARN] nn::detail::UnexpectedDefaultImpl(%s, %s:%d)",
             func ? func : "", file ? file : "", line);
}

}

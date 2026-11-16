#pragma once
#include <sys/mman.h>
#include <cstdint>
#include <cstring>

constexpr uint64_t SWITCH_TLS_MAGIC = 0x535749544348544CULL; // "SWITCHTL"

inline uint64_t getCurrentTpidrEl0() {
    uint64_t val;
    asm volatile("mrs %0, tpidr_el0" : "=r"(val));
    return val;
}

inline void setCurrentTpidrEl0(uint64_t val) {
    asm volatile("msr tpidr_el0, %0" : : "r"(val));
}

inline uint8_t* createSwitchTlsBlock(const uint8_t* tlsInitData, size_t tlsFileSz, size_t tlsMemSz, void* threadHandle) {
    size_t totalTlsAlloc = (tlsMemSz + 4096 > 65536) ? (tlsMemSz + 4096) : 65536;
    uint8_t* block = reinterpret_cast<uint8_t*>(
        mmap(nullptr, totalTlsAlloc, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0)
    );
    if (block == MAP_FAILED) return nullptr;
    memset(block, 0, totalTlsAlloc);
    *reinterpret_cast<void**>(block + 0x1E0) = threadHandle;
    *reinterpret_cast<uint64_t*>(block + 0x1E8) = SWITCH_TLS_MAGIC;
    *reinterpret_cast<uint64_t*>(block + 0x1F8) = reinterpret_cast<uint64_t>(block + 0x200);
    if (tlsInitData && tlsFileSz > 0) {
        memcpy(block + 0x200, tlsInitData, tlsFileSz);
    }
    return block;
}

inline void bindAndroidTlsToSwitchTls(uint8_t* switchTls, uint64_t androidTls) {
    if (switchTls) {
        *reinterpret_cast<uint64_t*>(switchTls + 0x1F0) = androidTls;
    }
}

inline uint64_t getThreadAndroidTls() {
    uint64_t cur = getCurrentTpidrEl0();
    if (cur != 0 && (cur & 0xFFF) == 0) {
        uint64_t magic = *reinterpret_cast<uint64_t*>(cur + 0x1E8);
        if (magic == SWITCH_TLS_MAGIC) {
            uint64_t saved = *reinterpret_cast<uint64_t*>(cur + 0x1F0);
            if (saved != 0) return saved;
        }
    }
    return cur;
}

inline void* getThreadSwitchHandle() {
    uint64_t cur = getCurrentTpidrEl0();
    if (cur != 0 && (cur & 0xFFF) == 0) {
        uint64_t magic = *reinterpret_cast<uint64_t*>(cur + 0x1E8);
        if (magic == SWITCH_TLS_MAGIC) {
            return *reinterpret_cast<void**>(cur + 0x1E0);
        }
    }
    return nullptr;
}

struct ScopedAndroidTls {
    uint64_t prevTls;
    bool switched;
    ScopedAndroidTls() : prevTls(getCurrentTpidrEl0()), switched(false) {
        if (prevTls != 0 && (prevTls & 0xFFF) == 0) {
            uint64_t magic = *reinterpret_cast<uint64_t*>(prevTls + 0x1E8);
            if (magic == SWITCH_TLS_MAGIC) {
                uint64_t saved = *reinterpret_cast<uint64_t*>(prevTls + 0x1F0);
                if (saved != 0 && saved != prevTls) {
                    setCurrentTpidrEl0(saved);
                    switched = true;
                }
            }
        }
    }
    ~ScopedAndroidTls() {
        if (switched) {
            setCurrentTpidrEl0(prevTls);
        }
    }
};

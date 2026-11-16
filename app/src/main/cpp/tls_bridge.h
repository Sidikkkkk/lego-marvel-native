#pragma once
#include <cstdint>

inline uint64_t g_AndroidTls = 0;

inline uint64_t getCurrentTpidrEl0() {
    uint64_t val;
    asm volatile("mrs %0, tpidr_el0" : "=r"(val));
    return val;
}

inline void setCurrentTpidrEl0(uint64_t val) {
    asm volatile("msr tpidr_el0, %0" : : "r"(val));
}

struct ScopedAndroidTls {
    uint64_t prevTls;
    ScopedAndroidTls() {
        prevTls = getCurrentTpidrEl0();
        if (g_AndroidTls != 0 && prevTls != g_AndroidTls) {
            setCurrentTpidrEl0(g_AndroidTls);
        }
    }
    ~ScopedAndroidTls() {
        if (prevTls != 0 && prevTls != g_AndroidTls) {
            setCurrentTpidrEl0(prevTls);
        }
    }
};

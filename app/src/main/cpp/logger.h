#pragma once
#include <android/log.h>
#include <cstdio>
#include <cstdarg>
#include <string>

inline FILE* g_LogFile = nullptr;
inline std::string g_BaseDataDir;

inline void initLogger(const char* dirPath) {
    if (g_LogFile) {
        fclose(g_LogFile);
        g_LogFile = nullptr;
    }
    g_BaseDataDir = dirPath;
    std::string logPath = g_BaseDataDir + "/boot_log.txt";
    g_LogFile = fopen(logPath.c_str(), "w");
}

inline void writeLog(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    __android_log_vprint(ANDROID_LOG_INFO, "LegoLoader", fmt, args);
    va_end(args);

    if (g_LogFile) {
        va_start(args, fmt);
        vfprintf(g_LogFile, fmt, args);
        fprintf(g_LogFile, "\n");
        fflush(g_LogFile);
        va_end(args);
    }
}

inline void closeLogger() {
    if (g_LogFile) {
        fclose(g_LogFile);
        g_LogFile = nullptr;
    }
}

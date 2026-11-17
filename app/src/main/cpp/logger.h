#pragma once
#include <android/log.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <string>

inline int g_LogFd = -1;
inline std::string g_BaseDataDir;

inline void initLogger(const char* dirPath) {
    if (g_LogFd >= 0) {
        close(g_LogFd);
        g_LogFd = -1;
    }
    g_BaseDataDir = dirPath;
    std::string logPath = g_BaseDataDir + "/boot_log.txt";
    g_LogFd = open(logPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
}

inline void writeLog(const char* fmt, ...) {
    char buf[2048];
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(buf, sizeof(buf) - 2, fmt, args);
    va_end(args);

    if (len < 0) return;
    if (len >= static_cast<int>(sizeof(buf) - 2)) {
        len = static_cast<int>(sizeof(buf) - 2);
    }
    buf[len] = '\0';

    __android_log_write(ANDROID_LOG_INFO, "LegoLoader", buf);

    if (g_LogFd >= 0) {
        buf[len] = '\n';
        write(g_LogFd, buf, static_cast<size_t>(len + 1));
        fdatasync(g_LogFd);
    }
}

inline void closeLogger() {
    if (g_LogFd >= 0) {
        fsync(g_LogFd);
        close(g_LogFd);
        g_LogFd = -1;
    }
}

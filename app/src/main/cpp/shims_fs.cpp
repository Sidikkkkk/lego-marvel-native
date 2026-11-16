#include "logger.h"
#include "device_profile.h"
#include "tls_bridge.h"
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <cstring>
#include <cstdint>
#include <string>
#include <algorithm>

struct HostFileHandle {
    int fd;
    int openMode;
};

struct HostDirHandle {
    DIR* dir;
    std::string hostPath;
};

static std::string mapSwitchPathToAndroid(const char* switchPath) {
    std::string base = g_BaseDataDir.empty() ? "/sdcard/LegoPort" : g_BaseDataDir;
    if (!switchPath) return base + "/romfs";

    std::string p(switchPath);
    if (p.rfind("romfs:/", 0) == 0) {
        p = p.substr(7);
        return base + "/romfs/" + p;
    }
    if (p.rfind("rom:/", 0) == 0) {
        p = p.substr(5);
        return base + "/romfs/" + p;
    }
    if (p.rfind("romfs:", 0) == 0) {
        p = p.substr(6);
        while (!p.empty() && p[0] == '/') p.erase(0, 1);
        return base + "/romfs/" + p;
    }
    if (p.rfind("rom:", 0) == 0) {
        p = p.substr(4);
        while (!p.empty() && p[0] == '/') p.erase(0, 1);
        return base + "/romfs/" + p;
    }
    if (p.rfind("save:/", 0) == 0 || p.rfind("savedata:/", 0) == 0) {
        size_t pos = p.find(":/");
        p = p.substr(pos + 2);
        return base + "/save/" + p;
    }
    while (!p.empty() && (p[0] == '/' || p[0] == '.')) {
        if (p.rfind("./", 0) == 0) p = p.substr(2);
        else p.erase(0, 1);
    }
    return base + "/romfs/" + p;
}

extern "C" {

uint32_t shim_nn_fs_QueryMountRomCacheSize(size_t* outCacheSize) {
    ScopedAndroidTls tls;
    if (outCacheSize) *outCacheSize = 64 * 1024;
    writeLog("[FS] nn::fs::QueryMountRomCacheSize() -> 64 KB");
    return 0;
}

uint32_t shim_nn_fs_MountRom(const char* name, void* cacheBuffer, uint64_t cacheSize) {
    ScopedAndroidTls tls;
    writeLog("[FS] nn::fs::MountRom(name=%s) -> %s/romfs",
             name ? name : "rom", g_BaseDataDir.c_str());
    return 0;
}

uint32_t shim_nn_fs_MountSaveData(const char* name, const void* uid) {
    ScopedAndroidTls tls;
    std::string saveDir = (g_BaseDataDir.empty() ? "/sdcard/LegoPort" : g_BaseDataDir) + "/save";
    mkdir(saveDir.c_str(), 0777);
    writeLog("[FS] nn::fs::MountSaveData(name=%s) -> %s", name ? name : "save", saveDir.c_str());
    return 0;
}

uint32_t shim_nn_fs_EnsureSaveData(const void* uid) {
    ScopedAndroidTls tls;
    std::string saveDir = (g_BaseDataDir.empty() ? "/sdcard/LegoPort" : g_BaseDataDir) + "/save";
    mkdir(saveDir.c_str(), 0777);
    return 0;
}

uint32_t shim_nn_fs_CommitSaveData(const char* name) {
    return 0;
}

uint32_t shim_nn_fs_OpenFile(HostFileHandle** outHandle, const char* path, int mode) {
    ScopedAndroidTls tls;
    std::string hostPath = mapSwitchPathToAndroid(path);

    int flags = O_RDONLY;
    if ((mode & 2) && (mode & 1)) flags = O_RDWR | O_CREAT;
    else if (mode & 2) flags = O_WRONLY | O_CREAT;

    int fd = open(hostPath.c_str(), flags, 0666);
    if (fd < 0) {
        writeLog("[FS] OpenFile('%s' -> '%s', mode=%d) -> NOT FOUND", path ? path : "", hostPath.c_str(), mode);
        return 0x202; // ResultPathNotFound
    }

    auto* h = new HostFileHandle{fd, mode};
    if (outHandle) *outHandle = h;
    writeLog("[FS] OpenFile('%s' -> '%s', mode=%d) -> OK (fd=%d)", path ? path : "", hostPath.c_str(), mode, fd);
    return 0;
}

uint32_t shim_nn_fs_GetFileSize(int64_t* outSize, HostFileHandle* handle) {
    ScopedAndroidTls tls;
    if (!handle || handle->fd < 0) return 0x202;
    struct stat st{};
    if (fstat(handle->fd, &st) != 0) return 0x202;
    if (outSize) *outSize = static_cast<int64_t>(st.st_size);
    return 0;
}

uint32_t shim_nn_fs_ReadFileOut(size_t* outBytesRead, HostFileHandle* handle, int64_t offset, void* buffer, size_t size) {
    ScopedAndroidTls tls;
    if (!handle || handle->fd < 0 || !buffer) return 0x202;
    ssize_t n = pread(handle->fd, buffer, size, static_cast<off_t>(offset));
    if (n < 0) return 0x202;
    if (outBytesRead) *outBytesRead = static_cast<size_t>(n);
    return 0;
}

uint32_t shim_nn_fs_ReadFileNoOut(HostFileHandle* handle, int64_t offset, void* buffer, size_t size) {
    size_t dummy = 0;
    return shim_nn_fs_ReadFileOut(&dummy, handle, offset, buffer, size);
}

uint32_t shim_nn_fs_WriteFile(HostFileHandle* handle, int64_t offset, const void* buffer, size_t size, const void* option) {
    ScopedAndroidTls tls;
    if (!handle || handle->fd < 0 || !buffer) return 0x202;
    ssize_t n = pwrite(handle->fd, buffer, size, static_cast<off_t>(offset));
    return (n >= 0) ? 0 : 0x202;
}

uint32_t shim_nn_fs_FlushFile(HostFileHandle* handle) {
    ScopedAndroidTls tls;
    if (handle && handle->fd >= 0) fsync(handle->fd);
    return 0;
}

void shim_nn_fs_CloseFile(HostFileHandle* handle) {
    ScopedAndroidTls tls;
    if (handle) {
        if (handle->fd >= 0) close(handle->fd);
        delete handle;
    }
}

uint32_t shim_nn_fs_GetEntryType(uint32_t* outType, const char* path) {
    ScopedAndroidTls tls;
    std::string hostPath = mapSwitchPathToAndroid(path);
    struct stat st{};
    if (stat(hostPath.c_str(), &st) != 0) {
        return 0x202;
    }
    if (outType) {
        *outType = S_ISDIR(st.st_mode) ? 0U : 1U;
    }
    return 0;
}

uint32_t shim_nn_fs_OpenDirectory(HostDirHandle** outHandle, const char* path, int mode) {
    ScopedAndroidTls tls;
    std::string hostPath = mapSwitchPathToAndroid(path);
    DIR* d = opendir(hostPath.c_str());
    if (!d) return 0x202;
    if (outHandle) *outHandle = new HostDirHandle{d, hostPath};
    return 0;
}

uint32_t shim_nn_fs_ReadDirectory(int64_t* outCount, void* entryBuffer, HostDirHandle* handle, int64_t maxEntries) {
    ScopedAndroidTls tls;
    if (!handle || !handle->dir || maxEntries <= 0) {
        if (outCount) *outCount = 0;
        return 0;
    }
    int64_t count = 0;
    uint8_t* dst = reinterpret_cast<uint8_t*>(entryBuffer);
    while (count < maxEntries) {
        struct dirent* ent = readdir(handle->dir);
        if (!ent) break;
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        // nn::fs::DirectoryEntry: char name[768], pad[3], uint8_t type, pad[3], int64_t fileSize (total 784 bytes)
        uint8_t* cur = dst + (count * 784);
        memset(cur, 0, 784);
        strncpy(reinterpret_cast<char*>(cur), ent->d_name, 767);
        cur[771] = (ent->d_type == DT_DIR) ? 0 : 1;
        count++;
    }
    if (outCount) *outCount = count;
    return 0;
}

void shim_nn_fs_CloseDirectory(HostDirHandle* handle) {
    ScopedAndroidTls tls;
    if (handle) {
        if (handle->dir) closedir(handle->dir);
        delete handle;
    }
}

uint32_t shim_nn_fs_CreateFile(const char* path, int64_t size) {
    ScopedAndroidTls tls;
    std::string hostPath = mapSwitchPathToAndroid(path);
    int fd = open(hostPath.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) return 0x202;
    if (size > 0) ftruncate(fd, static_cast<off_t>(size));
    close(fd);
    return 0;
}

uint32_t shim_nn_fs_CreateDirectory(const char* path) {
    ScopedAndroidTls tls;
    std::string hostPath = mapSwitchPathToAndroid(path);
    mkdir(hostPath.c_str(), 0777);
    return 0;
}

uint32_t shim_nn_fs_DeleteFile(const char* path) {
    ScopedAndroidTls tls;
    std::string hostPath = mapSwitchPathToAndroid(path);
    unlink(hostPath.c_str());
    return 0;
}

}

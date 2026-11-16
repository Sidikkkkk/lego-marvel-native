#include <android_native_app_glue.h>
#include <sys/stat.h>
#include "logger.h"
#include "device_profile.h"
#include "elf_loader.h"

void android_main(struct android_app* state) {
    const char* extDir = state->activity->externalDataPath;
    mkdir(extDir, 0777);
    initLogger(extDir);

    writeLog("=== LEGO MARVEL UNIVERSAL NATIVE LOADER ===");
    detectDeviceProfile();

    std::string elfPath = std::string(extDir) + "/main.elf";
    void* entryPoint = loadSwitchElf(elfPath.c_str());

    if (entryPoint) {
        writeLog("[BOOT] Menjalankan entry point main.elf di %p...", entryPoint);
        using EntryFn = void (*)();
        reinterpret_cast<EntryFn>(entryPoint)();
    }

    closeLogger();
}

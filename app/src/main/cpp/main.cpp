#include <android_native_app_glue.h>
#include <android/native_window.h>
#include <android/log.h>
#include <jni.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/stat.h>
#include <string>
#include <atomic>
#include "logger.h"
#include "device_profile.h"
#include "elf_loader.h"

static ANativeWindow* g_AndroidWindow = nullptr;
static std::atomic<int> g_BootStage{0};
static int g_LastRenderedStage = -1;

static void renderStatusSurface(bool force = false) {
    ANativeWindow* win = g_AndroidWindow;
    int stage = g_BootStage.load();
    if (!win || (!force && stage == g_LastRenderedStage)) return;
    g_LastRenderedStage = stage;

    ANativeWindow_setBuffersGeometry(win, 320, 180, WINDOW_FORMAT_RGBA_8888);
    ANativeWindow_Buffer buf{};
    if (ANativeWindow_lock(win, &buf, nullptr) != 0) return;

    auto* pixels = reinterpret_cast<uint32_t*>(buf.bits);
    const uint32_t bg = 0xFF100B08;
    const uint32_t barBg = 0xFF2A1E16;
    uint32_t barColor = (stage == 1) ? 0xFF00A5FF : (stage == 2 ? 0xFF50E030 : 0xFF4040FF);
    int barWidth = (stage == 1) ? 100 : (stage == 2 ? 200 : 260);

    for (int y = 0; y < buf.height; ++y) {
        uint32_t* row = pixels + y * buf.stride;
        for (int x = 0; x < buf.width; ++x) {
            if (y >= 82 && y <= 98 && x >= 30 && x <= 290) {
                row[x] = (x <= 30 + barWidth) ? barColor : barBg;
            } else {
                row[x] = bg;
            }
        }
    }
    ANativeWindow_unlockAndPost(win);
}

static bool hasStoragePermission(JNIEnv* env) {
    jclass versionClass = env->FindClass("android/os/Build$VERSION");
    jfieldID sdkIntField = env->GetStaticFieldID(versionClass, "SDK_INT", "I");
    jint sdkInt = env->GetStaticIntField(versionClass, sdkIntField);
    env->DeleteLocalRef(versionClass);

    if (sdkInt >= 30) {
        jclass envClass = env->FindClass("android/os/Environment");
        jmethodID isMgrMethod = env->GetStaticMethodID(envClass, "isExternalStorageManager", "()Z");
        jboolean isMgr = env->CallStaticBooleanMethod(envClass, isMgrMethod);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            isMgr = JNI_FALSE;
        }
        env->DeleteLocalRef(envClass);
        return isMgr == JNI_TRUE;
    }
    return access("/sdcard/LegoPort", W_OK) == 0;
}

static bool tryLaunchSettingsIntent(JNIEnv* env, jobject activity, const char* action, const char* uriString) {
    jclass intentClass = env->FindClass("android/content/Intent");
    jstring actionStr = env->NewStringUTF(action);
    jobject intent = nullptr;

    if (uriString != nullptr) {
        jclass uriClass = env->FindClass("android/net/Uri");
        jmethodID parseMethod = env->GetStaticMethodID(uriClass, "parse", "(Ljava/lang/String;)Landroid/net/Uri;");
        jstring uriStr = env->NewStringUTF(uriString);
        jobject uri = env->CallStaticObjectMethod(uriClass, parseMethod, uriStr);

        jmethodID intentCtor = env->GetMethodID(intentClass, "<init>", "(Ljava/lang/String;Landroid/net/Uri;)V");
        intent = env->NewObject(intentClass, intentCtor, actionStr, uri);

        env->DeleteLocalRef(uriStr);
        env->DeleteLocalRef(uri);
        env->DeleteLocalRef(uriClass);
    } else {
        jmethodID intentCtor = env->GetMethodID(intentClass, "<init>", "(Ljava/lang/String;)V");
        intent = env->NewObject(intentClass, intentCtor, actionStr);
    }

    jclass actClass = env->GetObjectClass(activity);
    jmethodID startAct = env->GetMethodID(actClass, "startActivity", "(Landroid/content/Intent;)V");
    env->CallVoidMethod(activity, startAct, intent);

    bool ok = true;
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        ok = false;
    }

    env->DeleteLocalRef(actionStr);
    env->DeleteLocalRef(intent);
    env->DeleteLocalRef(actClass);
    env->DeleteLocalRef(intentClass);
    return ok;
}

static void requestStoragePermission(JNIEnv* env, jobject activity) {
    jclass versionClass = env->FindClass("android/os/Build$VERSION");
    jfieldID sdkIntField = env->GetStaticFieldID(versionClass, "SDK_INT", "I");
    jint sdkInt = env->GetStaticIntField(versionClass, sdkIntField);
    env->DeleteLocalRef(versionClass);

    if (sdkInt >= 30) {
        if (!tryLaunchSettingsIntent(env, activity, "android.settings.MANAGE_APP_ALL_FILES_ACCESS_PERMISSION", "package:com.scratch.legomarvel")) {
            if (!tryLaunchSettingsIntent(env, activity, "android.settings.MANAGE_ALL_FILES_ACCESS_PERMISSION", nullptr)) {
                tryLaunchSettingsIntent(env, activity, "android.settings.APPLICATION_DETAILS_SETTINGS", "package:com.scratch.legomarvel");
            }
        }
    } else {
        jclass strClass = env->FindClass("java/lang/String");
        jobjectArray perms = env->NewObjectArray(2, strClass, nullptr);
        env->SetObjectArrayElement(perms, 0, env->NewStringUTF("android.permission.READ_EXTERNAL_STORAGE"));
        env->SetObjectArrayElement(perms, 1, env->NewStringUTF("android.permission.WRITE_EXTERNAL_STORAGE"));

        jclass actClass = env->GetObjectClass(activity);
        jmethodID reqPerm = env->GetMethodID(actClass, "requestPermissions", "([Ljava/lang/String;I)V");
        env->CallVoidMethod(activity, reqPerm, perms, 100);
        if (env->ExceptionCheck()) env->ExceptionClear();

        env->DeleteLocalRef(perms);
        env->DeleteLocalRef(actClass);
        env->DeleteLocalRef(strClass);
    }
}

static void handleAppCmd(struct android_app* app, int32_t cmd) {
    if (cmd == APP_CMD_INIT_WINDOW && app->window != nullptr) {
        g_AndroidWindow = app->window;
        writeLog("[ANDROID] Surface Window siap (%dx%d)",
                 ANativeWindow_getWidth(app->window), ANativeWindow_getHeight(app->window));
        renderStatusSurface(true);
    } else if (cmd == APP_CMD_TERM_WINDOW) {
        g_AndroidWindow = nullptr;
    }
}

static void* gameRunnerThread(void* arg) {
    installThreadAltStack();
    g_BootStage.store(1);
    void* entryPoint = loadSwitchElf("/sdcard/LegoPort/exefs/main.elf");
    if (entryPoint) {
        g_BootStage.store(2);
        jumpToSwitchEntry(entryPoint);
    }
    g_BootStage.store(3);
    writeLog("[BOOT] gameRunnerThread selesai.");
    return nullptr;
}

void android_main(struct android_app* state) {
    state->onAppCmd = handleAppCmd;

    JNIEnv* env = nullptr;
    state->activity->vm->AttachCurrentThread(&env, nullptr);

    if (!hasStoragePermission(env)) {
        requestStoragePermission(env, state->activity->clazz);
    }

    bool booted = false;
    while (state->destroyRequested == 0) {
        int events = 0;
        struct android_poll_source* source = nullptr;
        ALooper_pollOnce(150, nullptr, &events, reinterpret_cast<void**>(&source));
        if (source != nullptr) {
            source->process(state, source);
        }

        renderStatusSurface(false);

        if (!booted && hasStoragePermission(env)) {
            booted = true;
            const char* baseDir = "/sdcard/LegoPort";
            mkdir(baseDir, 0777);
            initLogger(baseDir);

            writeLog("=== LEGO MARVEL UNIVERSAL NATIVE LOADER ===");
            detectDeviceProfile();
            installCrashAndSvcHandlers();

            pthread_t gameThread;
            pthread_attr_t attr;
            pthread_attr_init(&attr);
            pthread_attr_setstacksize(&attr, 4 * 1024 * 1024);
            pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
            pthread_create(&gameThread, &attr, gameRunnerThread, nullptr);
            pthread_attr_destroy(&attr);
        }
    }

    closeLogger();
    state->activity->vm->DetachCurrentThread();
}

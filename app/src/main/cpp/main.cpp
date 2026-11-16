#include <android_native_app_glue.h>
#include <android/log.h>
#include <jni.h>
#include <unistd.h>
#include <sys/stat.h>
#include <string>
#include "logger.h"
#include "device_profile.h"
#include "elf_loader.h"

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

void android_main(struct android_app* state) {
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

        if (!booted && hasStoragePermission(env)) {
            booted = true;
            const char* baseDir = "/sdcard/LegoPort";
            mkdir(baseDir, 0777);
            initLogger(baseDir);

            writeLog("=== LEGO MARVEL UNIVERSAL NATIVE LOADER ===");
            detectDeviceProfile();
            installCrashAndSvcHandlers();

            void* entryPoint = loadSwitchElf("/sdcard/LegoPort/exefs/main.elf");
            if (entryPoint) {
                writeLog("[BOOT] Menjalankan entry point main.elf di %p...", entryPoint);
                using EntryFn = void (*)();
                reinterpret_cast<EntryFn>(entryPoint)();
                writeLog("[BOOT] Entry point selesai dieksekusi.");
            }
            closeLogger();
        }
    }

    state->activity->vm->DetachCurrentThread();
}

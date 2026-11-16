plugins {
    id("com.android.application")
}

android {
    namespace = "com.scratch.legomarvel"
    compileSdk = 34

    defaultConfig {
        applicationId = "com.scratch.legomarvel"
        minSdk = 24
        targetSdk = 34
        versionCode = 1
        versionName = "0.1-universal"
        ndk { abiFilters += listOf("arm64-v8a") }
        externalNativeBuild {
            cmake { cppFlags += "-std=c++20 -O2" }
        }
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }
}

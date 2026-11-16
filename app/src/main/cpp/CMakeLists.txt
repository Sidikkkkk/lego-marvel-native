cmake_minimum_required(VERSION 3.22.1)
project("legoloader")
set(CMAKE_CXX_STANDARD 20)

add_library(legoloader SHARED
    main.cpp
    elf_loader.cpp
    symbol_table.cpp
    shims_os.cpp
    shims_fs.cpp
    shims_nvn.cpp
)

add_library(native_app_glue STATIC
    ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c
)
target_include_directories(native_app_glue PUBLIC
    ${ANDROID_NDK}/sources/android/native_app_glue
)

target_link_libraries(legoloader native_app_glue android vulkan log)

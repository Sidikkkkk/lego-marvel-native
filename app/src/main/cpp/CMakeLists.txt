cmake_minimum_required(VERSION 3.22.1)
project("legoloader" C CXX)
set(CMAKE_CXX_STANDARD 20)

set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -u ANativeActivity_onCreate")

add_library(legoloader SHARED
    ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c
    main.cpp
    elf_loader.cpp
    symbol_table.cpp
    shims_os.cpp
    shims_fs.cpp
    shims_nvn.cpp shims_sdk.cpp
)

target_include_directories(legoloader PRIVATE
    ${ANDROID_NDK}/sources/android/native_app_glue
)

target_link_libraries(legoloader android vulkan log)

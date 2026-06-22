cmake_minimum_required(VERSION 3.18)

add_library(eventsdk_jni SHARED
    src/main/cpp/EventSDK_JNI.cpp
    ../../sdk-core/src/EventQueue.cpp
    ../../sdk-core/src/EventStore.cpp
    ../../sdk-core/src/FlushManager.cpp
)

target_include_directories(eventsdk_jni PRIVATE
    ../../sdk-core/src
)

target_compile_options(eventsdk_jni PRIVATE -std=c++17 -Wall -Wextra)

target_link_libraries(eventsdk_jni
    android
    log
)

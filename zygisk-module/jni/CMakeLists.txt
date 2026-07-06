cmake_minimum_required(VERSION 3.22.1)
project("amkush_hook" CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT ANDROID_ABI MATCHES "arm64-v8a|x86_64")
    message(FATAL_ERROR "Only arm64-v8a and x86_64 are supported. Got: ${ANDROID_ABI}")
endif()

if(NOT DEFINED ANDROID_PLATFORM)
    set(ANDROID_PLATFORM "android-28")
endif()

# ── Compiler flags ────────────────────────────────────────────────────────────
# -fno-sanitize=cfi          : prevent CFI from aborting inline hooks
# -fvisibility=hidden        : only __attribute__((visibility("default"))) exported
# -fno-exceptions            : camera hooks cannot use C++ exceptions safely
# -fno-rtti                  : reduces binary size; we use explicit casts
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} \
    -fno-sanitize=cfi \
    -fvisibility=hidden \
    -fno-exceptions \
    -fno-rtti \
    -Wno-unused-parameter \
    -Wall")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} \
    -fno-sanitize=cfi \
    -fvisibility=hidden \
    -Wall")

# ── Source files ───────────────────────────────────────────────────────────────
set(HOOK_SOURCES
    main.cpp
    hook_proxy.cpp
    symbol_resolver.cpp
    stream_map.cpp
    frame_source.cpp
    frame_inject.cpp
    crash_guard.cpp
    ipc_socket.cpp
)

# ── Main hook library ──────────────────────────────────────────────────────────
add_library(hookProxy SHARED ${HOOK_SOURCES})

# ── Include paths ──────────────────────────────────────────────────────────────
target_include_directories(hookProxy PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include   # camera3_compat.h
    ${CMAKE_CURRENT_SOURCE_DIR}           # zygisk.hpp, *.h
)

# ── ShadowHook — ByteDance inline hook library ─────────────────────────────────
# In build.gradle for the Zygisk module app:
#   implementation 'com.bytedance.android:shadowhook:2.0.1'
#   buildFeatures { prefab true }
find_package(shadowhook REQUIRED CONFIG)
target_link_libraries(hookProxy shadowhook::shadowhook)

# ── libyuv ────────────────────────────────────────────────────────────────────
# Preferred: prefab from 'io.github.leleliu008:libyuv-android:0.0.2'
# Fallback:  place libyuv.a + libyuv/ headers in third_party/libyuv/
find_package(libyuv QUIET CONFIG)
if(libyuv_FOUND)
    target_link_libraries(hookProxy libyuv::yuv)
else()
    message(STATUS "libyuv prefab not found — using bundled static library")
    add_library(yuv STATIC IMPORTED)
    set_target_properties(yuv PROPERTIES
        IMPORTED_LOCATION
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libyuv/libs/${ANDROID_ABI}/libyuv.a"
    )
    target_include_directories(hookProxy PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/libyuv/include
    )
    target_link_libraries(hookProxy yuv)
endif()

# ── libjpeg-turbo ─────────────────────────────────────────────────────────────
find_library(TURBOJPEG_LIB turbojpeg)
if(TURBOJPEG_LIB)
    target_link_libraries(hookProxy ${TURBOJPEG_LIB})
else()
    message(STATUS "turbojpeg not in NDK sysroot — looking for bundled prebuilt")
    add_library(turbojpeg STATIC IMPORTED)
    set_target_properties(turbojpeg PROPERTIES
        IMPORTED_LOCATION
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/turbojpeg/libs/${ANDROID_ABI}/libturbojpeg.a"
    )
    target_include_directories(hookProxy PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/turbojpeg/include
    )
    target_link_libraries(hookProxy turbojpeg)
endif()

# ── NDK / system libraries ────────────────────────────────────────────────────
find_library(LOG_LIB     log)
find_library(ANDROID_LIB android)
find_library(SYNC_LIB    sync)

target_link_libraries(hookProxy
    ${LOG_LIB}
    ${ANDROID_LIB}
    ${SYNC_LIB}
)

# ── Strip debug symbols in release builds ─────────────────────────────────────
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_custom_command(TARGET hookProxy POST_BUILD
        COMMAND ${CMAKE_STRIP} --strip-all $<TARGET_FILE:hookProxy>
        COMMENT "Stripping hookProxy.so"
    )
endif()

set_target_properties(hookProxy PROPERTIES OUTPUT_NAME "hookProxy")

message(STATUS "amkush hookProxy configured for ${ANDROID_ABI} / ${ANDROID_PLATFORM}")

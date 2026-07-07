cmake_minimum_required(VERSION 3.22.1)
  project("amkush_hook" CXX C)

  set(CMAKE_CXX_STANDARD 17)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)

  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-sanitize=cfi -fvisibility=hidden -fno-exceptions -fno-rtti -Wall -Wno-unused-parameter")
  set(CMAKE_C_FLAGS   "${CMAKE_C_FLAGS}   -fno-sanitize=cfi -fvisibility=hidden -Wall")

  set(HOOK_SOURCES
      main.cpp hook_proxy.cpp symbol_resolver.cpp stream_map.cpp
      frame_source.cpp frame_inject.cpp crash_guard.cpp ipc_socket.cpp
  )

  add_library(hookProxy SHARED ${HOOK_SOURCES})

  target_include_directories(hookProxy PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${CMAKE_CURRENT_SOURCE_DIR}
  )

  find_library(LOG_LIB log)
  find_library(ANDROID_LIB android)
  target_link_libraries(hookProxy ${LOG_LIB} ${ANDROID_LIB})

  # ── ShadowHook ─────────────────────────────────────────────────────────────────
  # CI path: pass -DSHADOWHOOK_ROOT=<dir> containing include/ and <ABI>/lib*.
  # Gradle path: find_package via prefab (automatic when building with AGP).
  if(DEFINED SHADOWHOOK_ROOT)
      message(STATUS "ShadowHook: prebuilt at ${SHADOWHOOK_ROOT}")
      target_include_directories(hookProxy PRIVATE "${SHADOWHOOK_ROOT}/include")
      if(EXISTS "${SHADOWHOOK_ROOT}/${ANDROID_ABI}/libshadowhook.a")
          add_library(sh_lib STATIC IMPORTED)
          set_target_properties(sh_lib PROPERTIES IMPORTED_LOCATION "${SHADOWHOOK_ROOT}/${ANDROID_ABI}/libshadowhook.a")
      elseif(EXISTS "${SHADOWHOOK_ROOT}/${ANDROID_ABI}/libshadowhook.so")
          add_library(sh_lib SHARED IMPORTED)
          set_target_properties(sh_lib PROPERTIES IMPORTED_LOCATION "${SHADOWHOOK_ROOT}/${ANDROID_ABI}/libshadowhook.so")
      else()
          message(FATAL_ERROR "ShadowHook lib not found under ${SHADOWHOOK_ROOT}/${ANDROID_ABI}/")
      endif()
      target_link_libraries(hookProxy sh_lib)
  else()
      find_package(shadowhook REQUIRED CONFIG)
      target_link_libraries(hookProxy shadowhook::shadowhook)
  endif()

  # ── libyuv ────────────────────────────────────────────────────────────────────
  if(DEFINED LIBYUV_ROOT)
      message(STATUS "libyuv: prebuilt at ${LIBYUV_ROOT}")
      target_include_directories(hookProxy PRIVATE "${LIBYUV_ROOT}/include")
      add_library(yuv_lib STATIC IMPORTED)
      set_target_properties(yuv_lib PROPERTIES IMPORTED_LOCATION "${LIBYUV_ROOT}/${ANDROID_ABI}/libyuv.a")
      target_link_libraries(hookProxy yuv_lib)
  else()
      find_library(LIBYUV_LIB yuv PATHS ${CMAKE_CURRENT_SOURCE_DIR}/third_party/libyuv/libs/${ANDROID_ABI})
      if(LIBYUV_LIB)
          target_link_libraries(hookProxy ${LIBYUV_LIB})
          target_include_directories(hookProxy PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/libyuv/include)
      else()
          message(WARNING "libyuv not found — YUV conversion disabled")
          target_compile_definitions(hookProxy PRIVATE DISABLE_LIBYUV=1)
      endif()
  endif()

  # ── libjpeg-turbo ─────────────────────────────────────────────────────────────
  if(DEFINED TURBOJPEG_ROOT)
      message(STATUS "turbojpeg: prebuilt at ${TURBOJPEG_ROOT}")
      target_include_directories(hookProxy PRIVATE "${TURBOJPEG_ROOT}/include")
      add_library(tj_lib STATIC IMPORTED)
      set_target_properties(tj_lib PROPERTIES IMPORTED_LOCATION "${TURBOJPEG_ROOT}/${ANDROID_ABI}/libturbojpeg.a")
      target_link_libraries(hookProxy tj_lib)
  else()
      find_library(TJ_LIB turbojpeg)
      if(TJ_LIB)
          target_link_libraries(hookProxy ${TJ_LIB})
      else()
          message(WARNING "turbojpeg not found — JPEG snapshot injection disabled")
          target_compile_definitions(hookProxy PRIVATE DISABLE_JPEG_INJECTION=1)
      endif()
  endif()

  set_target_properties(hookProxy PROPERTIES OUTPUT_NAME "hookProxy")
  message(STATUS "hookProxy configured — ABI=${ANDROID_ABI}")
  
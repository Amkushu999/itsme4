// main.cpp — Zygisk module entry + LD_PRELOAD constructor for cameraserver.
  //
  // TWO LOAD PATHS:
  //
  //  Path A — Zygisk registered module:
  //    Loaded into Zygote. postAppSpecialize fires for every fork.
  //    NOTE: cameraserver is a native daemon started by init — it is NOT
  //    forked from Zygote, so this path DOES NOT fire for cameraserver.
  //    This path remains for future OEM variants that do fork it from Zygote.
  //
  //  Path B — LD_PRELOAD via wrap.cameraserver (PRIMARY path):
  //    service.sh sets:  resetprop wrap.cameraserver "LD_PRELOAD /system/lib64/libhookProxy.so"
  //    When init starts cameraserver it reads that property, applies LD_PRELOAD,
  //    and the __attribute__((constructor)) below runs inside cameraserver.
  //    ShadowHook then patches processCaptureResult directly in the HAL.

  #include <zygisk.hpp>
  #include <android/log.h>
  #include <string.h>
  #include <stdio.h>
  #include <jni.h>

  #include "hook_proxy.h"
  #include "frame_source.h"
  #include "frame_inject.h"
  #include "ipc_socket.h"
  #include "crash_guard.h"

  #define TAG "amkush/main"
  #define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  TAG, __VA_ARGS__)
  #define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

  using zygisk::Api;
  using zygisk::AppSpecializeArgs;
  using zygisk::ServerSpecializeArgs;

  static void do_camera_hook_init(const char *via) {
      LOGI("Installing camera hooks via %s", via);
      if (crash_guard_init() != 0)  LOGE("crash_guard_init failed");
      if (frame_inject_init() != 0) LOGE("frame_inject_init failed");
      if (hook_proxy_install() != 0) {
          LOGE("hook_proxy_install failed — virtual camera inactive");
          crash_guard_cleanup();
          return;
      }
      if (ipc_socket_start() != 0) LOGE("ipc_socket_start failed");
      LOGI("amkush virtual camera ACTIVE (%s)", via);
  }

  // Path B: runs automatically on LD_PRELOAD or dlopen.
  __attribute__((constructor))
  static void on_library_load(void) {
      char cmdline[256] = {};
      FILE *f = fopen("/proc/self/cmdline", "r");
      if (f) { fread(cmdline, 1, sizeof(cmdline) - 1, f); fclose(f); }
      // If not cameraserver, let the Zygisk module lifecycle handle it.
      if (!strstr(cmdline, "cameraserver")) return;
      LOGI("LD_PRELOAD in cameraserver (cmdline=%s)", cmdline);
      do_camera_hook_init("LD_PRELOAD");
  }

  // Path A: Zygisk module (belt-and-suspenders for MTK/OEM ROMs).
  class AmkushModule : public zygisk::ModuleBase {
  public:
      void onLoad(Api *api, JNIEnv *env) override {
          this->api = api; this->env = env;
      }
      void preAppSpecialize(AppSpecializeArgs *args) override {
          if (!args) { api->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY); return; }
          const char *nm = env->GetStringUTFChars(args->nice_name, nullptr);
          bool cam = nm && strstr(nm, "cameraserver");
          env->ReleaseStringUTFChars(args->nice_name, nm);
          if (!cam) api->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
          matched = cam;
      }
      void postAppSpecialize(const AppSpecializeArgs *) override {
          if (!matched) return;
          if (hook_proxy_is_active()) { LOGI("hook already active (LD_PRELOAD path ran first)"); return; }
          do_camera_hook_init("Zygisk");
      }
      void preServerSpecialize(ServerSpecializeArgs *) override {
          api->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
      }
  private:
      Api *api = nullptr; JNIEnv *env = nullptr; bool matched = false;
  };

  static void companion_handler(int) {}
  REGISTER_ZYGISK_MODULE(AmkushModule)
  REGISTER_ZYGISK_COMPANION(companion_handler)
  
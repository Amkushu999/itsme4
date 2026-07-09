// activation.cpp — FaceGate native license guard
  // URLs XOR-obfuscated; no plaintext server address in the binary.
  // HTTP performed via JNI → java.net.HttpURLConnection (no libcurl needed).
  // Token persisted as device-keyed XOR-encrypted file; NOT in SharedPreferences.

  #include <jni.h>
  #include <string>
  #include <vector>
  #include <cstring>
  #include <cstdio>
  #include <cstdlib>
  #include <sys/time.h>
  #include <android/log.h>

  #define LOG_TAG "fg_lic"
  #define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
  #define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)

  // ─── URL obfuscation (XOR key 0x5A) ─────────────────────────────────────────
  // Decoded: https://standing-panther-214.convex.site
  static const uint8_t BASE_URL_OBF[] = {
      0x32,0x2e,0x2e,0x2a,0x29,0x60,0x75,0x75,
      0x29,0x2e,0x3b,0x34,0x3e,0x33,0x34,0x3d,
      0x77,0x2a,0x3b,0x34,0x2e,0x32,0x3f,0x28,
      0x77,0x68,0x6b,0x6e,0x74,0x39,0x35,0x34,
      0x2c,0x3f,0x22,0x74,0x29,0x33,0x2e,0x3f
  };
  // Decoded: /api/validate_key
  static const uint8_t EP_VALIDATE[] = {
      0x75,0x3b,0x2a,0x33,0x75,0x2c,0x3b,0x36,
      0x33,0x3e,0x3b,0x2e,0x3f,0x05,0x31,0x3f,0x23
  };
  // Decoded: /api/verify_token
  static const uint8_t EP_VERIFY[] = {
      0x75,0x3b,0x2a,0x33,0x75,0x2c,0x3f,0x28,
      0x33,0x3c,0x23,0x05,0x2e,0x35,0x31,0x3f,0x34
  };

  static std::string xor_decode(const uint8_t *d, size_t n) {
      std::string r; r.reserve(n);
      for (size_t i = 0; i < n; i++) r += (char)(d[i] ^ 0x5A);
      return r;
  }

  // ─── Anti-tamper ─────────────────────────────────────────────────────────────
  static bool debugger_attached() {
      char buf[256] = {};
      FILE *f = fopen("/proc/self/status","r");
      if (!f) return false;
      while (fgets(buf, sizeof(buf), f)) {
          if (strncmp(buf,"TracerPid:",10)==0) { fclose(f); return atoi(buf+10)!=0; }
      }
      fclose(f); return false;
  }

  static bool frida_present() {
      FILE *f = fopen("/proc/self/maps","r");
      if (!f) return false;
      char line[512];
      while (fgets(line, sizeof(line), f)) {
          if (strstr(line,"frida")||strstr(line,"gum-js")||strstr(line,"linjector")) {
              fclose(f); return true;
          }
      }
      fclose(f); return false;
  }

  static bool tampered() { return debugger_attached() || frida_present(); }

  // ─── Android ID (device fingerprint component) ────────────────────────────────
  static std::string android_id(JNIEnv *env, jobject ctx) {
      jclass cSec = env->FindClass("android/provider/Settings$Secure");
      if (!cSec) { env->ExceptionClear(); return ""; }

      jmethodID mGet = env->GetStaticMethodID(cSec, "getString",
          "(Landroid/content/ContentResolver;Ljava/lang/String;)Ljava/lang/String;");
      if (!mGet) { env->ExceptionClear(); env->DeleteLocalRef(cSec); return ""; }

      jclass cCtx = env->FindClass("android/content/Context");
      if (!cCtx) { env->ExceptionClear(); env->DeleteLocalRef(cSec); return ""; }

      jmethodID mCR = env->GetMethodID(cCtx, "getContentResolver",
          "()Landroid/content/ContentResolver;");
      env->DeleteLocalRef(cCtx);
      if (!mCR) { env->ExceptionClear(); env->DeleteLocalRef(cSec); return ""; }

      jobject cr = env->CallObjectMethod(ctx, mCR);
      if (!cr || env->ExceptionCheck()) {
          env->ExceptionClear(); env->DeleteLocalRef(cSec); return "";
      }

      jstring k  = env->NewStringUTF("android_id");
      jstring id = (jstring)env->CallStaticObjectMethod(cSec, mGet, cr, k);
      if (env->ExceptionCheck()) { env->ExceptionClear(); id = nullptr; }
      env->DeleteLocalRef(k);
      env->DeleteLocalRef(cr);
      env->DeleteLocalRef(cSec);

      if (!id) return "";
      const char *c = env->GetStringUTFChars(id, nullptr);
      std::string r(c ? c : "");
      if (c) env->ReleaseStringUTFChars(id, c);
      env->DeleteLocalRef(id);
      return r;
  }

  // ─── Encrypted file storage ───────────────────────────────────────────────────
  static const uint8_t SALT[] = {
      0xFA,0x3C,0x7E,0x11,0xB2,0x5D,0x98,0xC4,
      0x01,0x6A,0xE3,0x77,0x2F,0x8B,0x44,0xD9
  };

  static void xor_crypt(uint8_t *data, size_t len, const std::string &dk) {
      size_t kl=dk.size(), sl=sizeof(SALT);
      for (size_t i=0; i<len; i++)
          data[i] ^= ((uint8_t)dk[i%kl] ^ SALT[i%sl]);
  }

  static std::string lic_path(JNIEnv *env, jobject ctx) {
      // Fallback path in case JNI lookups fail
      static const char *FALLBACK = "/data/data/com.itsme.amkush/files/fg_lic.bin";

      jclass cCtx = env->FindClass("android/content/Context");
      if (!cCtx) { env->ExceptionClear(); return FALLBACK; }

      jmethodID mFD = env->GetMethodID(cCtx, "getFilesDir", "()Ljava/io/File;");
      env->DeleteLocalRef(cCtx);
      if (!mFD) { env->ExceptionClear(); return FALLBACK; }

      jobject fd = env->CallObjectMethod(ctx, mFD);
      if (!fd || env->ExceptionCheck()) { env->ExceptionClear(); return FALLBACK; }

      jclass cF = env->FindClass("java/io/File");
      if (!cF) { env->ExceptionClear(); env->DeleteLocalRef(fd); return FALLBACK; }

      jmethodID mAP = env->GetMethodID(cF, "getAbsolutePath", "()Ljava/lang/String;");
      env->DeleteLocalRef(cF);
      if (!mAP) { env->ExceptionClear(); env->DeleteLocalRef(fd); return FALLBACK; }

      jstring jp = (jstring)env->CallObjectMethod(fd, mAP);
      if (env->ExceptionCheck()) { env->ExceptionClear(); jp = nullptr; }
      env->DeleteLocalRef(fd);

      if (!jp) return FALLBACK;
      const char *c = env->GetStringUTFChars(jp, nullptr);
      std::string p = std::string(c ? c : "/data/data/com.itsme.amkush/files") + "/fg_lic.bin";
      if (c) env->ReleaseStringUTFChars(jp, c);
      env->DeleteLocalRef(jp);
      return p;
  }

  // Format: TOKEN\x1FEXPIRY_MS\x1FTRIAL_FLAG\n
  static bool lic_save(JNIEnv *env, jobject ctx,
      const std::string &tok, long exp, bool trial) {
      std::string dk = android_id(env, ctx);
      if (dk.empty()) dk = "FG_FALLBACK_2024";
      std::string plain = tok+"\x1F"+std::to_string(exp)+"\x1F"+(trial?"1":"0")+"\n";
      std::vector<uint8_t> buf(plain.begin(),plain.end());
      xor_crypt(buf.data(),buf.size(),dk);
      std::string p = lic_path(env, ctx);
      FILE *f = fopen(p.c_str(),"wb");
      if (!f) { LOGE("lic_save: fopen failed %s",p.c_str()); return false; }
      fwrite(buf.data(),1,buf.size(),f); fclose(f);
      LOGD("lic_save: ok len=%zu",buf.size());
      return true;
  }

  static bool lic_load(JNIEnv *env, jobject ctx,
      std::string &tok, long &exp, bool &trial) {
      std::string p = lic_path(env, ctx);
      FILE *f = fopen(p.c_str(),"rb");
      if (!f) return false;
      fseek(f,0,SEEK_END); long sz=ftell(f); fseek(f,0,SEEK_SET);
      if (sz<=0||sz>4096) { fclose(f); return false; }
      std::vector<uint8_t> buf(sz);
      fread(buf.data(),1,sz,f); fclose(f);
      std::string dk = android_id(env, ctx);
      if (dk.empty()) dk = "FG_FALLBACK_2024";
      xor_crypt(buf.data(),buf.size(),dk);
      std::string plain(buf.begin(),buf.end());
      size_t p1=plain.find('\x1F');
      if (p1==std::string::npos) return false;
      size_t p2=plain.find('\x1F',p1+1);
      if (p2==std::string::npos) return false;
      tok = plain.substr(0,p1);
      try { exp = std::stol(plain.substr(p1+1,p2-p1-1)); } catch(...) { return false; }
      trial = (plain.size()>p2+1 && plain[p2+1]=='1');
      return true;
  }

  // ─── JNI HTTP POST (java.net.HttpURLConnection) ───────────────────────────────
  // Every FindClass / GetMethodID / NewObject result is null-checked before use.
  static std::string jni_post(JNIEnv *env, const std::string &url, const std::string &body) {
      // ── URL object ──
      jclass cURL = env->FindClass("java/net/URL");
      if (!cURL) { env->ExceptionClear(); return ""; }
      jmethodID mURLInit = env->GetMethodID(cURL, "<init>", "(Ljava/lang/String;)V");
      if (!mURLInit) { env->ExceptionClear(); env->DeleteLocalRef(cURL); return ""; }
      jstring jurl = env->NewStringUTF(url.c_str());
      jobject uObj = env->NewObject(cURL, mURLInit, jurl);
      env->DeleteLocalRef(jurl);
      if (!uObj || env->ExceptionCheck()) {
          env->ExceptionClear(); env->DeleteLocalRef(cURL); return "";
      }

      // ── openConnection ──
      jmethodID mOpenConn = env->GetMethodID(cURL, "openConnection",
                                              "()Ljava/net/URLConnection;");
      env->DeleteLocalRef(cURL);
      if (!mOpenConn) { env->ExceptionClear(); env->DeleteLocalRef(uObj); return ""; }
      jobject conn = env->CallObjectMethod(uObj, mOpenConn);
      env->DeleteLocalRef(uObj);
      if (!conn || env->ExceptionCheck()) { env->ExceptionClear(); return ""; }

      jclass cHC = env->FindClass("java/net/HttpURLConnection");
      if (!cHC) {
          env->ExceptionClear(); env->DeleteLocalRef(conn); return "";
      }

      // setRequestMethod("POST")
      jmethodID mSetMethod = env->GetMethodID(cHC, "setRequestMethod",
                                               "(Ljava/lang/String;)V");
      if (mSetMethod) {
          jstring jPOST = env->NewStringUTF("POST");
          env->CallVoidMethod(conn, mSetMethod, jPOST);
          if (env->ExceptionCheck()) env->ExceptionClear();
          env->DeleteLocalRef(jPOST);
      } else { env->ExceptionClear(); }

      // setDoOutput(true)
      jmethodID mSetDO = env->GetMethodID(cHC, "setDoOutput", "(Z)V");
      if (mSetDO) { env->CallVoidMethod(conn, mSetDO, JNI_TRUE); if (env->ExceptionCheck()) env->ExceptionClear(); }
      else env->ExceptionClear();

      // headers
      jmethodID mSRP = env->GetMethodID(cHC, "setRequestProperty",
                                          "(Ljava/lang/String;Ljava/lang/String;)V");
      if (mSRP) {
          auto hdr = [&](const char *k, const char *v) {
              jstring jk = env->NewStringUTF(k), jv = env->NewStringUTF(v);
              env->CallVoidMethod(conn, mSRP, jk, jv);
              if (env->ExceptionCheck()) env->ExceptionClear();
              env->DeleteLocalRef(jk); env->DeleteLocalRef(jv);
          };
          hdr("Content-Type", "application/json");
          hdr("User-Agent",   "Dalvik/2.1.0 (Linux; Android)");
      } else { env->ExceptionClear(); }

      // timeouts
      jmethodID mCT = env->GetMethodID(cHC, "setConnectTimeout", "(I)V");
      jmethodID mRT = env->GetMethodID(cHC, "setReadTimeout",    "(I)V");
      if (mCT) { env->CallVoidMethod(conn, mCT, (jint)30000); if (env->ExceptionCheck()) env->ExceptionClear(); }
      else env->ExceptionClear();
      if (mRT) { env->CallVoidMethod(conn, mRT, (jint)30000); if (env->ExceptionCheck()) env->ExceptionClear(); }
      else env->ExceptionClear();

      // ── write body ──
      jmethodID mGetOS = env->GetMethodID(cHC, "getOutputStream", "()Ljava/io/OutputStream;");
      if (!mGetOS) { env->ExceptionClear(); env->DeleteLocalRef(cHC); env->DeleteLocalRef(conn); return ""; }
      jobject os = env->CallObjectMethod(conn, mGetOS);
      if (!os || env->ExceptionCheck()) { env->ExceptionClear(); env->DeleteLocalRef(cHC); env->DeleteLocalRef(conn); return ""; }

      jclass cOS = env->FindClass("java/io/OutputStream");
      if (cOS) {
          jbyteArray ba = env->NewByteArray((jsize)body.size());
          if (ba) {
              env->SetByteArrayRegion(ba, 0, (jsize)body.size(), (const jbyte*)body.data());
              jmethodID mWrite = env->GetMethodID(cOS, "write", "([B)V");
              jmethodID mFlush = env->GetMethodID(cOS, "flush", "()V");
              jmethodID mClose = env->GetMethodID(cOS, "close", "()V");
              if (mWrite) { env->CallVoidMethod(os, mWrite, ba); if (env->ExceptionCheck()) env->ExceptionClear(); }
              else env->ExceptionClear();
              if (mFlush) { env->CallVoidMethod(os, mFlush);     if (env->ExceptionCheck()) env->ExceptionClear(); }
              else env->ExceptionClear();
              if (mClose) { env->CallVoidMethod(os, mClose);     if (env->ExceptionCheck()) env->ExceptionClear(); }
              else env->ExceptionClear();
              env->DeleteLocalRef(ba);
          }
          env->DeleteLocalRef(cOS);
      } else { env->ExceptionClear(); }
      env->DeleteLocalRef(os);

      // ── read response ──
      if (env->ExceptionCheck()) { env->ExceptionClear(); env->DeleteLocalRef(cHC); env->DeleteLocalRef(conn); return ""; }

      jmethodID mGetIS = env->GetMethodID(cHC, "getInputStream", "()Ljava/io/InputStream;");
      if (!mGetIS) { env->ExceptionClear(); env->DeleteLocalRef(cHC); env->DeleteLocalRef(conn); return ""; }
      jobject is = env->CallObjectMethod(conn, mGetIS);
      if (!is || env->ExceptionCheck()) { env->ExceptionClear(); env->DeleteLocalRef(cHC); env->DeleteLocalRef(conn); return ""; }

      jclass cISR = env->FindClass("java/io/InputStreamReader");
      if (!cISR) { env->ExceptionClear(); env->DeleteLocalRef(is); env->DeleteLocalRef(cHC); env->DeleteLocalRef(conn); return ""; }
      jmethodID mISRInit = env->GetMethodID(cISR, "<init>", "(Ljava/io/InputStream;)V");
      if (!mISRInit) { env->ExceptionClear(); env->DeleteLocalRef(cISR); env->DeleteLocalRef(is); env->DeleteLocalRef(cHC); env->DeleteLocalRef(conn); return ""; }
      jobject isr = env->NewObject(cISR, mISRInit, is);
      env->DeleteLocalRef(cISR);
      if (!isr || env->ExceptionCheck()) { env->ExceptionClear(); env->DeleteLocalRef(is); env->DeleteLocalRef(cHC); env->DeleteLocalRef(conn); return ""; }

      jclass cBR = env->FindClass("java/io/BufferedReader");
      if (!cBR) { env->ExceptionClear(); env->DeleteLocalRef(isr); env->DeleteLocalRef(is); env->DeleteLocalRef(cHC); env->DeleteLocalRef(conn); return ""; }
      jmethodID mBRInit = env->GetMethodID(cBR, "<init>", "(Ljava/io/Reader;)V");
      if (!mBRInit) { env->ExceptionClear(); env->DeleteLocalRef(cBR); env->DeleteLocalRef(isr); env->DeleteLocalRef(is); env->DeleteLocalRef(cHC); env->DeleteLocalRef(conn); return ""; }
      jobject br = env->NewObject(cBR, mBRInit, isr);
      env->DeleteLocalRef(isr);
      if (!br || env->ExceptionCheck()) { env->ExceptionClear(); env->DeleteLocalRef(cBR); env->DeleteLocalRef(is); env->DeleteLocalRef(cHC); env->DeleteLocalRef(conn); return ""; }
      jmethodID mRL = env->GetMethodID(cBR, "readLine", "()Ljava/lang/String;");
      env->DeleteLocalRef(cBR);
      if (!mRL) { env->ExceptionClear(); env->DeleteLocalRef(br); env->DeleteLocalRef(is); env->DeleteLocalRef(cHC); env->DeleteLocalRef(conn); return ""; }

      std::string resp;
      while (true) {
          jstring line = (jstring)env->CallObjectMethod(br,mRL);
          if (!line||env->ExceptionCheck()) { env->ExceptionClear(); break; }
          const char *ch = env->GetStringUTFChars(line,nullptr);
          resp += ch; env->ReleaseStringUTFChars(line,ch);
          env->DeleteLocalRef(line);
      }
      env->DeleteLocalRef(br);
      // isr already released after br construction; do not double-free
      env->DeleteLocalRef(is);
      jmethodID mDisc = env->GetMethodID(cHC, "disconnect", "()V");
      if (mDisc) { env->CallVoidMethod(conn, mDisc); if (env->ExceptionCheck()) env->ExceptionClear(); }
      else env->ExceptionClear();
      env->DeleteLocalRef(cHC);
      env->DeleteLocalRef(conn);
      return resp;
  }

  // ─── JNI exports ─────────────────────────────────────────────────────────────
  extern "C" {

  JNIEXPORT jstring JNICALL
  Java_com_itsme_amkush_security_LicenseGuard_nativeValidateKey(
      JNIEnv *env, jobject, jstring jKey, jstring jDeviceId, jstring jWifiIp) {
      if (tampered())
          return env->NewStringUTF("{\"success\":false,\"message\":\"Security check failed\",\"token\":null}");

      const char *key=env->GetStringUTFChars(jKey,nullptr);
      const char *did=env->GetStringUTFChars(jDeviceId,nullptr);
      const char *wip=jWifiIp?env->GetStringUTFChars(jWifiIp,nullptr):nullptr;

      std::string json="{\"key\":\""+std::string(key)+
                        "\",\"device_id\":\""+std::string(did)+"\"";
      if (wip&&strlen(wip)>0) json+=",\"wifi_ip\":\""+std::string(wip)+"\"";
      json+="}";

      env->ReleaseStringUTFChars(jKey,key);
      env->ReleaseStringUTFChars(jDeviceId,did);
      if (jWifiIp&&wip) env->ReleaseStringUTFChars(jWifiIp,wip);

      std::string url=xor_decode(BASE_URL_OBF,sizeof(BASE_URL_OBF))
                     +xor_decode(EP_VALIDATE,sizeof(EP_VALIDATE));
      std::string res=jni_post(env,url,json);
      if (res.empty()) res="{\"success\":false,\"message\":\"Network error\",\"token\":null}";
      LOGD("validateKey resp len=%zu",res.size());
      return env->NewStringUTF(res.c_str());
  }

  JNIEXPORT jstring JNICALL
  Java_com_itsme_amkush_security_LicenseGuard_nativeVerifyToken(
      JNIEnv *env, jobject, jstring jToken, jstring jDeviceId) {
      if (tampered())
          return env->NewStringUTF("{\"valid\":false,\"message\":\"Security check failed\"}");

      const char *tok=env->GetStringUTFChars(jToken,nullptr);
      const char *did=env->GetStringUTFChars(jDeviceId,nullptr);
      std::string json="{\"token\":\""+std::string(tok)+
                        "\",\"device_id\":\""+std::string(did)+"\"}" ;
      env->ReleaseStringUTFChars(jToken,tok);
      env->ReleaseStringUTFChars(jDeviceId,did);

      std::string url=xor_decode(BASE_URL_OBF,sizeof(BASE_URL_OBF))
                     +xor_decode(EP_VERIFY,sizeof(EP_VERIFY));
      std::string res=jni_post(env,url,json);
      if (res.empty()) res="{\"valid\":false,\"message\":\"Network error\"}";
      return env->NewStringUTF(res.c_str());
  }

  JNIEXPORT jboolean JNICALL
  Java_com_itsme_amkush_security_LicenseGuard_nativeIsActivated(
      JNIEnv *env, jobject, jobject ctx) {
      if (tampered()) return JNI_FALSE;
      std::string tok; long exp=0; bool trial=false;
      if (!lic_load(env,ctx,tok,exp,trial)||tok.empty()) return JNI_FALSE;
      if (exp>0) {
          struct timeval tv{}; gettimeofday(&tv,nullptr);
          long now=(long)tv.tv_sec*1000L+(long)tv.tv_usec/1000L;
          if (now>exp) { LOGD("nativeIsActivated: expired"); return JNI_FALSE; }
      }
      return JNI_TRUE;
  }

  JNIEXPORT jboolean JNICALL
  Java_com_itsme_amkush_security_LicenseGuard_nativeSaveActivation(
      JNIEnv *env, jobject, jobject ctx, jstring jTok, jboolean trial, jlong expMs) {
      const char *t=env->GetStringUTFChars(jTok,nullptr);
      bool ok=lic_save(env,ctx,std::string(t),(long)expMs,(bool)trial);
      env->ReleaseStringUTFChars(jTok,t);
      return ok?JNI_TRUE:JNI_FALSE;
  }

  JNIEXPORT void JNICALL
  Java_com_itsme_amkush_security_LicenseGuard_nativeClearActivation(
      JNIEnv *env, jobject, jobject ctx) {
      remove(lic_path(env,ctx).c_str());
  }

  JNIEXPORT jboolean JNICALL
  Java_com_itsme_amkush_security_LicenseGuard_nativeSecurityCheck(
      JNIEnv *env, jobject) {
      return tampered()?JNI_FALSE:JNI_TRUE;
  }

  // ── URL helpers: all URLs XOR-obfuscated; Kotlin never sees plaintext strings ──
  // ── Key 0x5A throughout. Decode: byte ^ 0x5A.                                ──

  JNIEXPORT jstring JNICALL
  Java_com_itsme_amkush_security_LicenseGuard_nativeGetBaseUrl(
      JNIEnv *env, jobject) {
      std::string url = xor_decode(BASE_URL_OBF, sizeof(BASE_URL_OBF));
      return env->NewStringUTF(url.c_str());
  }

  // Decoded: https://grateful-mule-939.convex.site/download  (XOR key 0x5A)
  static const uint8_t DOWNLOAD_URL_OBF[] = {
      0x32,0x2E,0x2E,0x2A,0x29,0x60,0x75,0x75,  // https://
      0x3D,0x28,0x3B,0x2E,0x3F,0x3C,0x2F,0x36,  // grateful
      0x77,                                        // -
      0x37,0x2F,0x36,0x3F,                        // mule
      0x77,                                        // -
      0x63,0x69,0x63,                              // 939
      0x74,                                        // .
      0x39,0x35,0x34,0x2C,0x3F,0x22,0x74,        // convex.
      0x29,0x33,0x2E,0x3F,                        // site
      0x75,                                        // /
      0x3E,0x35,0x2D,0x34,0x36,0x35,0x3B,0x3E   // download
  };

  JNIEXPORT jstring JNICALL
  Java_com_itsme_amkush_security_LicenseGuard_nativeGetDownloadUrl(
      JNIEnv *env, jobject) {
      std::string url = xor_decode(DOWNLOAD_URL_OBF, sizeof(DOWNLOAD_URL_OBF));
      return env->NewStringUTF(url.c_str());
  }

  // ── Telegram URLs (XOR key 0x5A) ─────────────────────────────────────────────

  // Decoded: https://t.me/Facegateofficialbot
  static const uint8_t TG_BOT_OBF[] = {
      0x32,0x2E,0x2E,0x2A,0x29,0x60,0x75,0x75,  // https://
      0x2E,0x74,0x37,0x3F,0x75,                  // t.me/
      0x1C,0x3B,0x39,0x3F,0x3D,0x3B,0x2E,0x3F,  // Facegate
      0x35,0x3C,0x3C,0x33,0x39,0x33,0x3B,0x36,  // official
      0x38,0x35,0x2E                              // bot
  };

  // Decoded: https://t.me/+Tx-rhbl-VcgyNDg0
  static const uint8_t TG_CHANNEL_OBF[] = {
      0x32,0x2E,0x2E,0x2A,0x29,0x60,0x75,0x75,  // https://
      0x2E,0x74,0x37,0x3F,0x75,                  // t.me/
      0x71,0x0E,0x22,0x77,0x28,0x32,0x38,0x36,  // +Tx-rhbl
      0x77,0x0C,0x39,0x3D,0x23,0x14,0x1E,0x3D,0x6A  // -VcgyNDg0
  };

  // Decoded: https://t.me/facegateofficial
  static const uint8_t TG_OWNER_OBF[] = {
      0x32,0x2E,0x2E,0x2A,0x29,0x60,0x75,0x75,  // https://
      0x2E,0x74,0x37,0x3F,0x75,                  // t.me/
      0x3C,0x3B,0x39,0x3F,0x3D,0x3B,0x2E,0x3F,  // facegate
      0x35,0x3C,0x3C,0x33,0x39,0x33,0x3B,0x36   // official
  };

  JNIEXPORT jstring JNICALL
  Java_com_itsme_amkush_security_LicenseGuard_nativeGetTgBot(
      JNIEnv *env, jobject) {
      return env->NewStringUTF(xor_decode(TG_BOT_OBF, sizeof(TG_BOT_OBF)).c_str());
  }

  JNIEXPORT jstring JNICALL
  Java_com_itsme_amkush_security_LicenseGuard_nativeGetTgChannel(
      JNIEnv *env, jobject) {
      return env->NewStringUTF(xor_decode(TG_CHANNEL_OBF, sizeof(TG_CHANNEL_OBF)).c_str());
  }

  JNIEXPORT jstring JNICALL
  Java_com_itsme_amkush_security_LicenseGuard_nativeGetTgOwner(
      JNIEnv *env, jobject) {
      return env->NewStringUTF(xor_decode(TG_OWNER_OBF, sizeof(TG_OWNER_OBF)).c_str());
  }

  } // extern "C"
  
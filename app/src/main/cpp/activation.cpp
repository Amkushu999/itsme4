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
      if (!cSec) return "";
      jmethodID mGet = env->GetStaticMethodID(cSec,"getString",
          "(Landroid/content/ContentResolver;Ljava/lang/String;)Ljava/lang/String;");
      if (!mGet) return "";
      jclass cCtx = env->FindClass("android/content/Context");
      jmethodID mCR = env->GetMethodID(cCtx,"getContentResolver",
          "()Landroid/content/ContentResolver;");
      jobject cr = env->CallObjectMethod(ctx, mCR);
      jstring k = env->NewStringUTF("android_id");
      jstring id = (jstring)env->CallStaticObjectMethod(cSec, mGet, cr, k);
      env->DeleteLocalRef(k);
      if (!id) return "";
      const char *c = env->GetStringUTFChars(id,nullptr);
      std::string r(c); env->ReleaseStringUTFChars(id,c);
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
      jclass cCtx = env->FindClass("android/content/Context");
      jmethodID mFD = env->GetMethodID(cCtx,"getFilesDir","()Ljava/io/File;");
      jobject fd = env->CallObjectMethod(ctx, mFD);
      jclass cF = env->FindClass("java/io/File");
      jmethodID mAP = env->GetMethodID(cF,"getAbsolutePath","()Ljava/lang/String;");
      jstring jp = (jstring)env->CallObjectMethod(fd, mAP);
      const char *c = env->GetStringUTFChars(jp,nullptr);
      std::string p = std::string(c)+"/fg_lic.bin";
      env->ReleaseStringUTFChars(jp,c);
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
  static std::string jni_post(JNIEnv *env, const std::string &url, const std::string &body) {
      // ── URL object ──
      jclass cURL = env->FindClass("java/net/URL");
      jstring jurl = env->NewStringUTF(url.c_str());
      jobject uObj = env->NewObject(cURL,
          env->GetMethodID(cURL,"<init>","(Ljava/lang/String;)V"), jurl);
      env->DeleteLocalRef(jurl);
      if (!uObj||env->ExceptionCheck()) { env->ExceptionClear(); return ""; }

      // ── openConnection ──
      jobject conn = env->CallObjectMethod(uObj,
          env->GetMethodID(cURL,"openConnection","()Ljava/net/URLConnection;"));
      env->DeleteLocalRef(uObj);
      if (!conn||env->ExceptionCheck()) { env->ExceptionClear(); return ""; }

      jclass cHC = env->FindClass("java/net/HttpURLConnection");
      // method
      jstring jPOST = env->NewStringUTF("POST");
      env->CallVoidMethod(conn,
          env->GetMethodID(cHC,"setRequestMethod","(Ljava/lang/String;)V"), jPOST);
      env->DeleteLocalRef(jPOST);
      // doOutput
      env->CallVoidMethod(conn,
          env->GetMethodID(cHC,"setDoOutput","(Z)V"), JNI_TRUE);
      // headers
      jmethodID mSRP = env->GetMethodID(cHC,"setRequestProperty",
          "(Ljava/lang/String;Ljava/lang/String;)V");
      auto hdr = [&](const char *k, const char *v){
          jstring jk=env->NewStringUTF(k), jv=env->NewStringUTF(v);
          env->CallVoidMethod(conn,mSRP,jk,jv);
          env->DeleteLocalRef(jk); env->DeleteLocalRef(jv);
      };
      hdr("Content-Type","application/json");
      hdr("User-Agent","Dalvik/2.1.0 (Linux; Android)");
      // timeouts
      env->CallVoidMethod(conn,env->GetMethodID(cHC,"setConnectTimeout","(I)V"),(jint)30000);
      env->CallVoidMethod(conn,env->GetMethodID(cHC,"setReadTimeout","(I)V"),(jint)30000);

      // ── write body ──
      jobject os = env->CallObjectMethod(conn,
          env->GetMethodID(cHC,"getOutputStream","()Ljava/io/OutputStream;"));
      if (!os||env->ExceptionCheck()) { env->ExceptionClear(); return ""; }
      jclass cOS = env->FindClass("java/io/OutputStream");
      jbyteArray ba = env->NewByteArray((jsize)body.size());
      env->SetByteArrayRegion(ba,0,(jsize)body.size(),(const jbyte*)body.data());
      env->CallVoidMethod(os,env->GetMethodID(cOS,"write","([B)V"),ba);
      env->CallVoidMethod(os,env->GetMethodID(cOS,"flush","()V"));
      env->CallVoidMethod(os,env->GetMethodID(cOS,"close","()V"));
      env->DeleteLocalRef(ba); env->DeleteLocalRef(os);

      // ── read response ──
      if (env->ExceptionCheck()) { env->ExceptionClear(); return ""; }
      jobject is = env->CallObjectMethod(conn,
          env->GetMethodID(cHC,"getInputStream","()Ljava/io/InputStream;"));
      if (!is||env->ExceptionCheck()) { env->ExceptionClear(); return ""; }

      jclass cISR = env->FindClass("java/io/InputStreamReader");
      jobject isr = env->NewObject(cISR,
          env->GetMethodID(cISR,"<init>","(Ljava/io/InputStream;)V"), is);
      jclass cBR = env->FindClass("java/io/BufferedReader");
      jobject br = env->NewObject(cBR,
          env->GetMethodID(cBR,"<init>","(Ljava/io/Reader;)V"), isr);
      jmethodID mRL = env->GetMethodID(cBR,"readLine","()Ljava/lang/String;");

      std::string resp;
      while (true) {
          jstring line = (jstring)env->CallObjectMethod(br,mRL);
          if (!line||env->ExceptionCheck()) { env->ExceptionClear(); break; }
          const char *ch = env->GetStringUTFChars(line,nullptr);
          resp += ch; env->ReleaseStringUTFChars(line,ch);
          env->DeleteLocalRef(line);
      }
      env->DeleteLocalRef(br); env->DeleteLocalRef(isr); env->DeleteLocalRef(is);
      env->CallVoidMethod(conn,env->GetMethodID(cHC,"disconnect","()V"));
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

  // ── URL helpers: return XOR-decoded server addresses so Kotlin never contains ──
  // ── plaintext URL strings. Callers: LicenseGuard.nativeGetBaseUrl() etc.      ──

  JNIEXPORT jstring JNICALL
  Java_com_itsme_amkush_security_LicenseGuard_nativeGetBaseUrl(
      JNIEnv *env, jobject) {
      std::string url = xor_decode(BASE_URL_OBF, sizeof(BASE_URL_OBF));
      return env->NewStringUTF(url.c_str());
  }

  // Decoded: /download  (XOR key 0x5A)
  static const uint8_t EP_DOWNLOAD[] = {
      0x75,0x3E,0x35,0x2D,0x34,0x36,0x35,0x3B,0x3E
  };

  JNIEXPORT jstring JNICALL
  Java_com_itsme_amkush_security_LicenseGuard_nativeGetDownloadUrl(
      JNIEnv *env, jobject) {
      std::string url = xor_decode(BASE_URL_OBF, sizeof(BASE_URL_OBF))
                      + xor_decode(EP_DOWNLOAD, sizeof(EP_DOWNLOAD));
      return env->NewStringUTF(url.c_str());
  }

  } // extern "C"
  
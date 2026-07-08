// license/license_client.h
//
// Two usage modes:
//
//   A) JNI — companion APK calls Java_*_nativeActivate / nativeVerify /
//             nativeHeartbeat.  A JNIEnv* is available; HTTP is made via
//             Java's HttpURLConnection (handles TLS automatically).
//
//   B) Pure-C++ — cameraserver verifies a token it received over the IPC
//             socket without making any network call.  Call
//             license_verify_local_token() only.
//
// IMPORTANT: set FACEGATE_HMAC_SECRET and FACEGATE_SERVER_URL to the same
// values that are configured on your Flask server before building.
//
// No exceptions, no RTTI — compatible with -fno-exceptions -fno-rtti.

#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <jni.h>

// ── Compile-time config ──────────────────────────────────────────────────────
//
// Bake FACEGATE_HMAC_SECRET into the APK.  It MUST match the value of
// FACEGATE_HMAC_SECRET on the Flask server.  Never log or transmit this.
//
#ifndef FACEGATE_HMAC_SECRET
#define FACEGATE_HMAC_SECRET \
    "a7f3c9e1b8d4025f6a4b9c0e7d1f8a3b5c2e6d9f0a1b4c7d8e2f5a9b3c6d0e7f"
#endif

#ifndef FACEGATE_SERVER_URL
#define FACEGATE_SERVER_URL "https://standing-panther-214.convex.site"
#endif

#define FACEGATE_TRIAL_KEY  "NOWORNEVER"

// ── Result struct ────────────────────────────────────────────────────────────

#ifdef __cplusplus
#include <string>

struct LicenseResult {
    bool        envelope_ok;       // HMAC signature verified (false = tampered/network error)
    bool        ok;                // server granted access
    char        access[8];         // "paid" | "trial" | "none" | ""
    char        token[72];         // paid token (hex, 64 chars) or ""
    char        reason[32];        // error code if !ok, e.g. "trial_exhausted"
    bool        destruct;          // app must stop camera hooks immediately
    int32_t     remaining_seconds; // trial only; -1 if not applicable
    char        expires_at[36];    // ISO-8601 or ""
};

// Zero-initialise a result (safe default = no access)
inline LicenseResult lr_deny(const char *reason_code = "unknown") {
    LicenseResult r{};
    r.envelope_ok        = false;
    r.ok                 = false;
    r.destruct           = true;
    r.remaining_seconds  = -1;
    r.access[0]          = '\0';
    r.token[0]           = '\0';
    r.expires_at[0]      = '\0';
    // safe strncpy
    size_t n = 0;
    while (reason_code[n] && n < sizeof(r.reason) - 1) {
        r.reason[n] = reason_code[n]; n++;
    }
    r.reason[n] = '\0';
    return r;
}
#endif // __cplusplus

// ── C-linkage interface (usable from cameraserver, no JVM needed) ────────────
#ifdef __cplusplus
extern "C" {
#endif

// Verify a paid token locally using HMAC — NO network call.
// Returns 1 if valid, 0 if not.
// token     : the hex token received from the server at activation time
// device_id : the device_id that was used to activate
// Call from cameraserver after receiving the token over the IPC socket.
int license_verify_local_token(const char *token,
                                const char *device_id,
                                const char *key_text,
                                int         key_id);

#ifdef __cplusplus
}
#endif

// ── JNI exports (companion APK only) ─────────────────────────────────────────
//
// Java class: com.facegate.license.LicenseClient   (change to your package)
// Usage:
//   System.loadLibrary("hookProxy");   // or a separate lib if you split it
//   String json = LicenseClient.nativeActivate(context, key);
//   // parse the JSON with Gson/Moshi, then verify the envelope in Java:
//   //   verifyEnvelope(json, ridYouSent) — or just trust the native layer
//   //   which already verified it and returns "" on failure.
//
// The native functions return a JSON string on success, or an empty string
// if the HMAC envelope check fails (tampered/replayed response).
// The JSON is the verified payload (the "p" object from the server envelope).
// ─────────────────────────────────────────────────────────────────────────────
#ifdef __cplusplus
extern "C" {
#endif

// Activate a key (or start a trial with "NOWORNEVER").
// context : android.content.Context
// key     : activation key string (jstring)
// Returns verified payload JSON string, or "" on HMAC failure.
JNIEXPORT jstring JNICALL
Java_com_facegate_license_LicenseClient_nativeActivate(
        JNIEnv *env, jclass /*clazz*/,
        jobject context, jstring key);

// Periodic access check. Call every ~5 minutes.
// token: the paid token (jstring), or null/empty for trial.
// Returns verified payload JSON, or "".
JNIEXPORT jstring JNICALL
Java_com_facegate_license_LicenseClient_nativeVerify(
        JNIEnv *env, jclass /*clazz*/,
        jobject context, jstring token);

// Lightweight heartbeat — trial only, call every 60 seconds.
// Returns verified payload JSON, or "".
JNIEXPORT jstring JNICALL
Java_com_facegate_license_LicenseClient_nativeHeartbeat(
        JNIEnv *env, jclass /*clazz*/,
        jobject context);

#ifdef __cplusplus
}
#endif

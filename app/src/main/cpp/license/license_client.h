// license/license_client.h  (APK-side copy — JNI class: com.itsme.amkush.license.LicenseClient)
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
// IMPORTANT: set FACEGATE_HMAC_SECRET to the same value as FACEGATE_HMAC_SECRET
// on your Flask server before building. Never log or transmit this value.
//
// Server URL is XOR-obfuscated in license_client.cpp — do NOT use a plaintext define.
//
// No exceptions, no RTTI — compatible with -fno-exceptions -fno-rtti.
//
// SECURITY NOTE: The HMAC secret is embedded in the APK for MITM resistance.
// An attacker who extracts the APK can forge responses.  Upgrade path:
//   Replace HMAC-SHA256 with ECDSA: keep only the *public* key in the APK;
//   the server signs with the private key.  Extraction then cannot forge responses.

#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <jni.h>

// ── Compile-time config ──────────────────────────────────────────────────────
//
// Bake FACEGATE_HMAC_SECRET into the APK.  It MUST match the value of
// FACEGATE_HMAC_SECRET on the Flask server.  Never log or transmit this.
//
// UPGRADE PATH: Replace with ECDSA public key to prevent forgery even if
// the APK is reverse-engineered. With ECDSA, only the *public* key lives
// here; the private key never leaves the server.
//
#ifndef FACEGATE_HMAC_SECRET
#define FACEGATE_HMAC_SECRET \
    "CHANGE_THIS_TO_64_RANDOM_CHARS_SAME_AS_SERVER_ENV_VAR_xxxxxxxxxxxxxxxxxxxxxxxx"
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
int license_verify_local_token(const char *token,
                                const char *device_id,
                                const char *key_text,
                                int         key_id);

#ifdef __cplusplus
}
#endif

// ── JNI exports (companion APK only) ─────────────────────────────────────────
//
// Java class: com.itsme.amkush.license.LicenseClient
// Usage:
//   System.loadLibrary("frame_producer");
//   String json = LicenseClient.nativeActivate(context, key);
//   // "" on failure (HMAC envelope check failed)
//   // non-empty = verified payload JSON (the "p" object)
//
#ifdef __cplusplus
extern "C" {
#endif

JNIEXPORT jstring JNICALL
Java_com_itsme_amkush_license_LicenseClient_nativeActivate(
        JNIEnv *env, jclass /*clazz*/,
        jobject context, jstring key);

JNIEXPORT jstring JNICALL
Java_com_itsme_amkush_license_LicenseClient_nativeVerify(
        JNIEnv *env, jclass /*clazz*/,
        jobject context, jstring token);

JNIEXPORT jstring JNICALL
Java_com_itsme_amkush_license_LicenseClient_nativeHeartbeat(
        JNIEnv *env, jclass /*clazz*/,
        jobject context);

#ifdef __cplusplus
}
#endif

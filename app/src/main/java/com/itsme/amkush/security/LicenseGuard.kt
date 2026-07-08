package com.itsme.amkush.security

  import android.content.Context
  import org.json.JSONObject

  object LicenseGuard {

      init {
          try { System.loadLibrary("frame_producer") } catch (_: Throwable) {}
      }

      // ── Raw JNI (private — callers use the typed wrappers below) ─────────────
      private external fun nativeValidateKey(key: String, deviceId: String, wifiIp: String?): String
      private external fun nativeVerifyToken(token: String, deviceId: String): String

      // ── Native storage / security ─────────────────────────────────────────────
      external fun nativeIsActivated(context: Context): Boolean
      external fun nativeSaveActivation(context: Context, token: String, isTrial: Boolean, expiryMs: Long): Boolean
      external fun nativeClearActivation(context: Context)
      external fun nativeSecurityCheck(): Boolean

      // ── URL helpers (server URL is XOR-obfuscated in native) ─────────────────
      external fun nativeGetBaseUrl(): String
      external fun nativeGetDownloadUrl(): String

      // ── Typed result wrappers ─────────────────────────────────────────────────
      data class ActivationResult(
          val success: Boolean,
          val token: String?,
          val isTrial: Boolean,
          val expiresAt: String?,
          val message: String
      )

      data class VerifyResult(
          val valid: Boolean,
          val isTrial: Boolean,
          val expiresAt: String?,
          val message: String
      )

      fun validateKey(key: String, deviceId: String, wifiIp: String?): ActivationResult =
          try {
              val j = JSONObject(nativeValidateKey(key, deviceId, wifiIp))
              ActivationResult(
                  success   = j.optBoolean("success"),
                  token     = j.optString("token").takeIf { it.isNotEmpty() && it != "null" },
                  isTrial   = j.optBoolean("is_trial"),
                  expiresAt = j.optString("expires_at").takeIf { it.isNotEmpty() && it != "null" },
                  message   = j.optString("message", "Unknown error")
              )
          } catch (e: Throwable) {
              ActivationResult(false, null, false, null, e.message ?: "Parse error")
          }

      fun verifyToken(token: String, deviceId: String): VerifyResult =
          try {
              val j = JSONObject(nativeVerifyToken(token, deviceId))
              VerifyResult(
                  valid     = j.optBoolean("valid"),
                  isTrial   = j.optBoolean("is_trial"),
                  expiresAt = j.optString("expires_at").takeIf { it.isNotEmpty() && it != "null" },
                  message   = j.optString("message", "Unknown")
              )
          } catch (e: Throwable) {
              VerifyResult(false, false, null, e.message ?: "Parse error")
          }
  }

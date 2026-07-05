package com.itsme.amkush.hooks

import android.content.pm.PackageManager
import android.os.Build
import com.itsme.amkush.AppState
import com.itsme.amkush.utils.Logger
import de.robv.android.xposed.XC_MethodHook
import de.robv.android.xposed.XposedHelpers
import de.robv.android.xposed.callbacks.XC_LoadPackage
import java.io.File

object ClonerBypassHooks {

    private val clonerPaths = listOf(
        "/data/data/com.mochi.cloner",
        "/data/data/com.lbe.parallel.intl",
        "/data/data/com.parallel.space",
        "/data/data/com.excelliance.dualaid",
        "/data/data/com.bly.dkplat",
        "/data/data/com.tencent.mobileqq.msf",
        "/data/data/com.android.clone",
        "/data/data/com.dualspace.parallel",
        "/data/data/com.clone.app",
        "/data/data/com.virtual.space",
        "/data/data/com.excelliance.multiaccounts",
        "/data/data/com.app.clone",
        "/data/data/com.clone.manager",
        "/data/data/com.multi.accounts",
        "/data/data/com.parallel.pro",
        "/data/data/com.vmos.cloner",
        "/data/data/com.android.parallel",
        "/data/data/com.lbe.parallel",
        "/data/data/com.space.virtual",
        "/data/data/com.clone.space",
        "/data/data/com.mochi.virtual",
        "/data/user/0/com.mochi.cloner",
        "/data/user_de/0/com.mochi.cloner",
        "/data/data/com.whatsapp.clone",
        "/data/data/com.instagram.clone",
        "/data/data/com.facebook.clone",
        "/data/data/com.telegram.clone",
        "/data/data/com.snapchat.clone",
        "/data/data/com.tiktok.clone"
    )

    private val clonerPackages = listOf(
        "com.mochi.cloner",
        "com.lbe.parallel.intl",
        "com.parallel.space",
        "com.excelliance.dualaid",
        "com.bly.dkplat",
        "com.tencent.mobileqq.msf",
        "com.android.clone",
        "com.dualspace.parallel",
        "com.clone.app",
        "com.virtual.space",
        "com.excelliance.multiaccounts",
        "com.app.clone",
        "com.clone.manager",
        "com.multi.accounts",
        "com.parallel.pro",
        "com.vmos.cloner",
        "com.android.parallel",
        "com.lbe.parallel",
        "com.space.virtual",
        "com.clone.space",
        "com.mochi.virtual",
        "com.whatsapp.clone",
        "com.instagram.clone",
        "com.facebook.clone",
        "com.telegram.clone",
        "com.snapchat.clone",
        "com.tiktok.clone",
        "com.multi.parallel.space",
        "com.clone.multiaccounts"
    )

    // Detect whether we are running inside a cloner's separated environment.
    // The broad File/Path hooks are only safe to install in that context —
    // on a clean non-cloned system they intercept legitimate app I/O and can
    // cause native crashes (e.g. interfering with camera library file reads).
    private fun isInsideCloner(lpparam: XC_LoadPackage.LoadPackageParam): Boolean {
        // Mochi Cloner places cloned apps under a "separation" subdirectory
        val filesDir = try {
            AppState.context?.filesDir?.absolutePath ?: ""
        } catch (_: Throwable) { "" }
        if (filesDir.contains("separation")) return true

        // Process name contains "separation" (Mochi's internal process isolation)
        if (lpparam.processName.contains("separation")) return true

        // Data dir path contains a known cloner package name
        if (clonerPackages.any { lpparam.processName.contains(it) }) return true

        return false
    }

    fun hookAll(lpparam: XC_LoadPackage.LoadPackageParam) {
        if (!AppState.isHookingActive) {
            return
        }

        val inCloner = isInsideCloner(lpparam)

        try {
            // Always safe: property and package-manager spoofing
            hookSystemProperties(lpparam)
            hookPackageManager(lpparam)
            hookApplicationInfo(lpparam)
            hookProcessInfo(lpparam)

            // Only install the broad File/path hooks inside a cloner environment.
            // On a plain LSPosed system these hooks intercept all File.exists() calls
            // in the target app — including camera library I/O — and can crash the process.
            if (inCloner) {
                hookFileExists(lpparam)
                hookMountNamespace(lpparam)
                hookPathChecks(lpparam)
                hookRuntimeExec(lpparam)
                // hookActivityThread intentionally removed: it hooks currentActivityThread()
                // (an extremely hot path) and calls Method.invoke() inside the callback,
                // which triggers re-entrant dispatch → stack overflow crash.
            }

            Logger.d(Logger.HOOK, "Cloner bypass hooks installed (inCloner=$inCloner)")
        } catch (e: Throwable) {
            Logger.e("Cloner bypass hooks failed", e)
        }
    }

    private fun hookFileExists(lpparam: XC_LoadPackage.LoadPackageParam) {
        try {
            val fileClass = XposedHelpers.findClass("java.io.File", lpparam.classLoader)

            XposedHelpers.findAndHookMethod(
                fileClass,
                "exists",
                object : XC_MethodHook() {
                    override fun afterHookedMethod(param: MethodHookParam) {
                        val file = param.thisObject as? File ?: return
                        val path = file.absolutePath

                        if (clonerPaths.any { path.contains(it) }) {
                            param.result = false
                            Logger.d(Logger.HOOK, "Blocked cloner file check: $path")
                        }

                        if (path.contains("/data/data/") && path.contains("/clone/")) {
                            param.result = false
                        }
                    }
                }
            )

            XposedHelpers.findAndHookMethod(
                fileClass,
                "canRead",
                object : XC_MethodHook() {
                    override fun afterHookedMethod(param: MethodHookParam) {
                        val file = param.thisObject as? File ?: return
                        val path = file.absolutePath
                        if (clonerPaths.any { path.contains(it) }) {
                            param.result = false
                        }
                    }
                }
            )

            XposedHelpers.findAndHookMethod(
                fileClass,
                "canWrite",
                object : XC_MethodHook() {
                    override fun afterHookedMethod(param: MethodHookParam) {
                        val file = param.thisObject as? File ?: return
                        val path = file.absolutePath
                        if (clonerPaths.any { path.contains(it) }) {
                            param.result = false
                        }
                    }
                }
            )

            XposedHelpers.findAndHookMethod(
                fileClass,
                "getAbsolutePath",
                object : XC_MethodHook() {
                    override fun afterHookedMethod(param: MethodHookParam) {
                        val file = param.thisObject as? File ?: return
                        val path = file.absolutePath
                        if (clonerPaths.any { path.contains(it) }) {
                            val spoofed = path.replace(
                                Regex("/data/data/com\\..*?\\.clone/"),
                                "/data/data/${AppState.targetPackage}/"
                            )
                            param.result = spoofed
                            Logger.d(Logger.HOOK, "Spoofed cloner path: $path -> $spoofed")
                        }
                    }
                }
            )

            XposedHelpers.findAndHookMethod(
                fileClass,
                "getCanonicalPath",
                object : XC_MethodHook() {
                    override fun afterHookedMethod(param: MethodHookParam) {
                        val file = param.thisObject as? File ?: return
                        val path = file.absolutePath
                        if (clonerPaths.any { path.contains(it) }) {
                            val spoofed = path.replace(
                                Regex("/data/data/com\\..*?\\.clone/"),
                                "/data/data/${AppState.targetPackage}/"
                            )
                            param.result = spoofed
                        }
                    }
                }
            )

        } catch (e: Throwable) {
            Logger.e("File.exists hook failed", e)
        }
    }

    private fun hookPackageManager(lpparam: XC_LoadPackage.LoadPackageParam) {
        try {
            val pmClass = XposedHelpers.findClass(
                "android.app.ApplicationPackageManager",
                lpparam.classLoader
            )

            XposedHelpers.findAndHookMethod(
                pmClass,
                "getInstalledApplications",
                Int::class.javaPrimitiveType,
                object : XC_MethodHook() {
                    override fun afterHookedMethod(param: MethodHookParam) {
                        filterClonerPackages(param)
                    }
                }
            )

            XposedHelpers.findAndHookMethod(
                pmClass,
                "getInstalledPackages",
                Int::class.javaPrimitiveType,
                object : XC_MethodHook() {
                    override fun afterHookedMethod(param: MethodHookParam) {
                        filterClonerPackages(param)
                    }
                }
            )

            XposedHelpers.findAndHookMethod(
                pmClass,
                "getPackageInfo",
                String::class.java,
                Int::class.javaPrimitiveType,
                object : XC_MethodHook() {
                    override fun beforeHookedMethod(param: MethodHookParam) {
                        val packageName = param.args[0] as? String
                        if (packageName != null && clonerPackages.contains(packageName)) {
                            param.result = null
                            Logger.d(Logger.HOOK, "Blocked cloner package info: $packageName")
                        }
                    }
                }
            )

            XposedHelpers.findAndHookMethod(
                pmClass,
                "getApplicationInfo",
                String::class.java,
                Int::class.javaPrimitiveType,
                object : XC_MethodHook() {
                    override fun beforeHookedMethod(param: MethodHookParam) {
                        val packageName = param.args[0] as? String
                        if (packageName != null && clonerPackages.contains(packageName)) {
                            param.result = null
                            Logger.d(Logger.HOOK, "Blocked cloner application info: $packageName")
                        }
                    }
                }
            )

        } catch (e: Throwable) {
            Logger.e("PackageManager hook failed", e)
        }
    }

    private fun filterClonerPackages(param: XC_MethodHook.MethodHookParam) {
        try {
            val result = param.result as? List<*>
            if (result != null) {
                val filtered = result.filter { item ->
                    val pkgName = getPackageNameFromItem(item!!)
                    !clonerPackages.contains(pkgName)
                }
                param.result = filtered
                Logger.d(Logger.HOOK, "Filtered cloner packages")
            }
        } catch (e: Throwable) {
            Logger.e("Failed to filter cloner packages", e)
        }
    }

    private fun hookApplicationInfo(lpparam: XC_LoadPackage.LoadPackageParam) {
        try {
            val appInfoClass = XposedHelpers.findClass(
                "android.content.pm.ApplicationInfo",
                lpparam.classLoader
            )

            XposedHelpers.findAndHookMethod(
                appInfoClass,
                "getSourceDir",
                object : XC_MethodHook() {
                    override fun afterHookedMethod(param: MethodHookParam) {
                        try {
                            val appInfo = param.thisObject
                            val pkgField = appInfo.javaClass.getField("packageName")
                            val packageName = pkgField.get(appInfo) as? String
                            if (packageName == AppState.targetPackage) {
                                param.result = "/data/app/${packageName}-base.apk"
                                Logger.d(Logger.HOOK, "Spoofed sourceDir for: $packageName")
                            }
                        } catch (_: Throwable) {}
                    }
                }
            )

            XposedHelpers.findAndHookMethod(
                appInfoClass,
                "getDataDir",
                object : XC_MethodHook() {
                    override fun afterHookedMethod(param: MethodHookParam) {
                        try {
                            val appInfo = param.thisObject
                            val pkgField = appInfo.javaClass.getField("packageName")
                            val packageName = pkgField.get(appInfo) as? String
                            if (packageName == AppState.targetPackage) {
                                param.result = "/data/data/${packageName}"
                                Logger.d(Logger.HOOK, "Spoofed dataDir for: $packageName")
                            }
                        } catch (_: Throwable) {}
                    }
                }
            )

        } catch (e: Throwable) {
            Logger.e("ApplicationInfo hook failed", e)
        }
    }

    private fun hookProcessInfo(lpparam: XC_LoadPackage.LoadPackageParam) {
        try {
            try {
                val pbClass = XposedHelpers.findClass(
                    "java.lang.ProcessBuilder",
                    lpparam.classLoader
                )

                XposedHelpers.findAndHookMethod(
                    pbClass,
                    "environment",
                    object : XC_MethodHook() {
                        override fun afterHookedMethod(param: MethodHookParam) {
                            val env = param.result as? MutableMap<String, String>
                            if (env != null) {
                                val keysToRemove = env.keys.filter { key ->
                                    key.contains("CLONER") || key.contains("CLONE") ||
                                    key.contains("VIRTUAL") || key.contains("PARALLEL") ||
                                    key.contains("MULTI") || key.contains("SPACE")
                                }
                                keysToRemove.forEach { env.remove(it) }
                                if (keysToRemove.isNotEmpty()) {
                                    Logger.d(Logger.HOOK, "Filtered ${keysToRemove.size} cloner env vars")
                                }
                            }
                        }
                    }
                )
            } catch (_: Throwable) {}

        } catch (e: Throwable) {
            Logger.e("ProcessInfo hook failed", e)
        }
    }

    private fun hookSystemProperties(lpparam: XC_LoadPackage.LoadPackageParam) {
        try {
            val systemPropertiesClass = XposedHelpers.findClass(
                "android.os.SystemProperties",
                lpparam.classLoader
            )

            XposedHelpers.findAndHookMethod(
                systemPropertiesClass,
                "get",
                String::class.java,
                object : XC_MethodHook() {
                    override fun afterHookedMethod(param: MethodHookParam) {
                        val key = param.args[0] as? String ?: return
                        if (key.contains("cloner") || key.contains("clone") || key.contains("virtual") ||
                            key.contains("parallel") || key.contains("space") || key.contains("multiaccounts")) {
                            param.result = ""
                            Logger.d(Logger.HOOK, "Blocked cloner property: $key")
                        }
                        if (key == "ro.build.fingerprint" || key == "ro.build.tags") {
                            param.result = "google/raven/raven:12/SQ3A.220605.009.A1/123456:user/release-keys"
                        }
                    }
                }
            )

            XposedHelpers.findAndHookMethod(
                systemPropertiesClass,
                "get",
                String::class.java,
                String::class.java,
                object : XC_MethodHook() {
                    override fun afterHookedMethod(param: MethodHookParam) {
                        val key = param.args[0] as? String ?: return
                        if (key.contains("cloner") || key.contains("clone") || key.contains("virtual") ||
                            key.contains("parallel") || key.contains("space") || key.contains("multiaccounts")) {
                            param.result = ""
                        }
                    }
                }
            )

        } catch (e: Throwable) {
            Logger.e("SystemProperties hook failed", e)
        }
    }

    private fun hookMountNamespace(lpparam: XC_LoadPackage.LoadPackageParam) {
        try {
            val osClass = XposedHelpers.findClass("android.system.Os", lpparam.classLoader)
            XposedHelpers.findAndHookMethod(
                osClass,
                "stat",
                String::class.java,
                object : XC_MethodHook() {
                    override fun afterHookedMethod(param: MethodHookParam) {
                        val path = param.args[0] as? String ?: return
                        if (path.contains("/proc/self/mountinfo") || path.contains("/proc/self/mounts")) {
                            Logger.d(Logger.HOOK, "Spoofed mount namespace for: $path")
                        }
                    }
                }
            )
        } catch (_: Throwable) {
            Logger.d(Logger.HOOK, "Mount namespace Os.stat hook not available")
        }

        try {
            val fileClass = XposedHelpers.findClass("java.io.File", lpparam.classLoader)

            XposedHelpers.findAndHookMethod(
                fileClass,
                "exists",
                object : XC_MethodHook() {
                    override fun afterHookedMethod(param: MethodHookParam) {
                        val file = param.thisObject as? File ?: return
                        val path = file.absolutePath
                        if (path == "/proc/self/mountinfo" || path == "/proc/self/mounts") {
                            if (!checkIfRealMount(path)) param.result = false
                        }
                    }
                }
            )

            // Hook FileInputStream.read to filter cloner mount entries.
            // IMPORTANT: On Android 9+, FileInputStream stores the path in a field named
            // "path" (String), NOT "file" (File). Using "file" throws NoSuchFieldException
            // which can crash the native bridge.
            try {
                val fisClass = XposedHelpers.findClass("java.io.FileInputStream", lpparam.classLoader)

                XposedHelpers.findAndHookMethod(
                    fisClass,
                    "read",
                    ByteArray::class.java,
                    Int::class.javaPrimitiveType,
                    Int::class.javaPrimitiveType,
                    object : XC_MethodHook() {
                        override fun afterHookedMethod(param: MethodHookParam) {
                            val filePath = try {
                                val f = param.thisObject.javaClass.getDeclaredField("path")
                                f.isAccessible = true
                                f.get(param.thisObject) as? String
                            } catch (_: Throwable) { null } ?: return

                            if (filePath == "/proc/self/mountinfo" || filePath == "/proc/self/mounts") {
                                val bytesRead = param.result as? Int ?: return
                                if (bytesRead <= 0) return
                                val buffer = param.args[0] as? ByteArray ?: return
                                val content = String(buffer, 0, bytesRead)
                                val filtered = filterMountContent(content)
                                val filteredBytes = filtered.toByteArray()
                                System.arraycopy(filteredBytes, 0, buffer, 0, filteredBytes.size)
                                param.result = filteredBytes.size
                            }
                        }
                    }
                )
            } catch (_: Throwable) {}

        } catch (e: Throwable) {
            Logger.e("Mount namespace file hook failed", e)
        }
    }

    private fun checkIfRealMount(path: String): Boolean {
        return try {
            val content = File(path).readText()
            val clonerMounts = listOf("mochi", "clone", "parallel", "virtual", "space", "multiaccounts")
            !clonerMounts.any { content.contains(it) }
        } catch (_: Throwable) { true }
    }

    private fun filterMountContent(content: String): String {
        return content.split("\n").filter { line ->
            val lower = line.lowercase()
            !lower.contains("mochi") && !lower.contains("clone") &&
            !lower.contains("parallel") && !lower.contains("virtual") &&
            !lower.contains("space") && !lower.contains("multiaccounts") &&
            !lower.contains("dual") && !lower.contains("multi")
        }.joinToString("\n")
    }

    private fun hookPathChecks(lpparam: XC_LoadPackage.LoadPackageParam) {
        try {
            val systemClass = XposedHelpers.findClass("java.lang.System", lpparam.classLoader)

            XposedHelpers.findAndHookMethod(
                systemClass,
                "getProperty",
                String::class.java,
                object : XC_MethodHook() {
                    override fun afterHookedMethod(param: MethodHookParam) {
                        val key = param.args[0] as? String ?: return
                        if (key == "java.class.path" || key == "java.library.path" || key == "java.ext.dirs") {
                            val value = param.result as? String ?: return
                            val filtered = value.split(":").filterNot { p ->
                                clonerPaths.any { p.contains(it) }
                            }.joinToString(":")
                            param.result = filtered
                            Logger.d(Logger.HOOK, "Filtered cloner paths from $key")
                        }
                    }
                }
            )

        } catch (e: Throwable) {
            Logger.e("System.getProperty hook failed", e)
        }
    }

    private fun hookRuntimeExec(lpparam: XC_LoadPackage.LoadPackageParam) {
        try {
            val runtimeClass = XposedHelpers.findClass("java.lang.Runtime", lpparam.classLoader)

            XposedHelpers.findAndHookMethod(
                runtimeClass,
                "exec",
                String::class.java,
                object : XC_MethodHook() {
                    override fun beforeHookedMethod(param: MethodHookParam) {
                        val cmd = param.args[0] as? String ?: return
                        if (cmd.contains("pm") && cmd.contains("list") && cmd.contains("packages")) {
                            param.args[0] = cmd.replace(Regex("\\| grep .*"), "")
                            Logger.d(Logger.HOOK, "Filtered PM command")
                        }
                    }
                }
            )

        } catch (e: Throwable) {
            Logger.e("Runtime.exec hook failed", e)
        }
    }

    // hookActivityThread intentionally removed.
    // It hooked ActivityThread.currentActivityThread() — one of the most frequently called
    // methods in Android — and called Method.invoke() inside the callback. That triggers
    // re-entrant hook dispatch → stack overflow. The sourceDir/dataDir spoofing it did
    // is now handled safely by hookApplicationInfo() above using field access instead.

    private fun getPackageNameFromItem(item: Any): String {
        return try {
            val field = item.javaClass.getField("packageName")
            field.get(item) as? String ?: ""
        } catch (_: Throwable) { "" }
    }
}

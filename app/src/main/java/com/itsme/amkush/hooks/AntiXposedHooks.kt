package com.itsme.amkush.hooks

import com.itsme.amkush.AppState
import com.itsme.amkush.utils.Logger
import de.robv.android.xposed.XC_MethodHook
import de.robv.android.xposed.XposedHelpers
import de.robv.android.xposed.callbacks.XC_LoadPackage

object AntiXposedHooks {

    private val xposedClassNames = listOf(
        "de.robv.android.xposed.XposedBridge",
        "de.robv.android.xposed.XposedHelpers",
        "de.robv.android.xposed.XC_MethodHook",
        "de.robv.android.xposed.IXposedHookLoadPackage",
        "de.robv.android.xposed.XposedBridge\$",
        "org.lsposed.lspd",
        "org.lsposed.lspd.core",
        "de.robv.android.xposed.IXposedHookZygoteInit",
        "de.robv.android.xposed.IXposedHookInitPackageResources"
    )

    private val xposedPackages = listOf(
        "de.robv.android.xposed.installer",
        "org.lsposed.manager",
        "com.topjohnwu.magisk"
    )

    private val xposedProperties = listOf(
        "persist.sys.xposed", "persist.sys.lsposed",
        "xposed", "lsposed", "ro.xposed", "ro.lsposed"
    )

    // Re-entrancy guard — Pine and LSPosed both call ClassLoader.loadClass / Class.forName
    // internally during hook dispatch. Without this guard our hook fires recursively and
    // overflows the stack (confirmed crash cause in cam1.1 logs).
    private val inHook = ThreadLocal<Boolean>()

    fun hookAll(lpparam: XC_LoadPackage.LoadPackageParam) {
        if (!AppState.isHookingActive) {
            return
        }

        try {
            hookStackTrace(lpparam)
            hookClassForName(lpparam)
            hookSystemProperties(lpparam)
            hookPackageManager(lpparam)
            // hookMethodInvoke intentionally removed: Pine uses Method.invoke internally for
            // its bridge dispatch, so hooking it creates an infinite recursion → stack overflow.
            hookClassLoader(lpparam)
            Logger.d(Logger.HOOK, "Anti-Xposed hooks installed")
        } catch (e: Throwable) {
            Logger.e("Anti-Xposed hooks failed", e)
        }
    }

    private fun hookStackTrace(lpparam: XC_LoadPackage.LoadPackageParam) {
        try {
            val throwableClass = XposedHelpers.findClass("java.lang.Throwable", lpparam.classLoader)

            XposedHelpers.findAndHookMethod(
                throwableClass,
                "getStackTrace",
                object : XC_MethodHook() {
                    override fun afterHookedMethod(param: MethodHookParam) {
                        val stackTrace = param.result as? Array<StackTraceElement>
                        if (stackTrace != null) {
                            val filtered = stackTrace.filterNot { element ->
                                element.className.contains("de.robv.android.xposed") ||
                                element.className.contains("XposedBridge") ||
                                element.className.contains("XposedHelpers") ||
                                element.className.contains("XC_MethodHook") ||
                                element.className.contains("LSPosed") ||
                                element.className.contains("lspd") ||
                                element.className.contains("de.robv.android.xposed.IXposedHookLoadPackage")
                            }
                            param.result = filtered.toTypedArray()
                        }
                    }
                }
            )

            Logger.d(Logger.HOOK, "StackTrace hook installed")

        } catch (e: Throwable) {
            Logger.e("StackTrace hook failed", e)
        }
    }

    private fun hookClassForName(lpparam: XC_LoadPackage.LoadPackageParam) {
        try {
            val classClass = XposedHelpers.findClass("java.lang.Class", lpparam.classLoader)

            XposedHelpers.findAndHookMethod(
                classClass,
                "forName",
                String::class.java,
                object : XC_MethodHook() {
                    override fun beforeHookedMethod(param: MethodHookParam) {
                        if (inHook.get() == true) return
                        val className = param.args[0] as? String ?: return
                        if (xposedClassNames.any { className.contains(it) }) {
                            throw ClassNotFoundException(className)
                        }
                    }
                }
            )

            XposedHelpers.findAndHookMethod(
                classClass,
                "forName",
                String::class.java,
                Boolean::class.javaPrimitiveType,
                ClassLoader::class.java,
                object : XC_MethodHook() {
                    override fun beforeHookedMethod(param: MethodHookParam) {
                        if (inHook.get() == true) return
                        val className = param.args[0] as? String ?: return
                        if (xposedClassNames.any { className.contains(it) }) {
                            throw ClassNotFoundException(className)
                        }
                    }
                }
            )

            Logger.d(Logger.HOOK, "Class.forName hook installed")

        } catch (e: Throwable) {
            Logger.e("Class.forName hook failed", e)
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
                        if (xposedProperties.any { key.lowercase().contains(it) }) {
                            param.result = ""
                            Logger.d(Logger.HOOK, "Blocked Xposed property: $key")
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
                        if (xposedProperties.any { key.lowercase().contains(it) }) {
                            param.result = ""
                        }
                    }
                }
            )

            Logger.d(Logger.HOOK, "SystemProperties hook installed")

        } catch (e: Throwable) {
            Logger.e("SystemProperties hook failed", e)
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
                        filterXposedPackages(param)
                    }
                }
            )

            XposedHelpers.findAndHookMethod(
                pmClass,
                "getInstalledPackages",
                Int::class.javaPrimitiveType,
                object : XC_MethodHook() {
                    override fun afterHookedMethod(param: MethodHookParam) {
                        filterXposedPackages(param)
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
                        if (packageName != null && xposedPackages.contains(packageName)) {
                            param.result = null
                            Logger.d(Logger.HOOK, "Blocked Xposed package info: $packageName")
                        }
                    }
                }
            )

            Logger.d(Logger.HOOK, "PackageManager hook installed")

        } catch (e: Throwable) {
            Logger.e("PackageManager hook failed", e)
        }
    }

    private fun filterXposedPackages(param: XC_MethodHook.MethodHookParam) {
        try {
            val result = param.result as? List<*>
            if (result != null) {
                val filtered = result.filter { item ->
                    val pkgName = getPackageNameFromItem(item!!)
                    !xposedPackages.contains(pkgName)
                }
                param.result = filtered
                Logger.d(Logger.HOOK, "Filtered Xposed packages")
            }
        } catch (e: Throwable) {
            Logger.e("Failed to filter Xposed packages", e)
        }
    }

    private fun hookClassLoader(lpparam: XC_LoadPackage.LoadPackageParam) {
        try {
            val classLoaderClass = XposedHelpers.findClass("java.lang.ClassLoader", lpparam.classLoader)

            XposedHelpers.findAndHookMethod(
                classLoaderClass,
                "loadClass",
                String::class.java,
                object : XC_MethodHook() {
                    override fun beforeHookedMethod(param: MethodHookParam) {
                        if (inHook.get() == true) return
                        inHook.set(true)
                        try {
                            val className = param.args[0] as? String ?: return
                            if (xposedClassNames.any { className.contains(it) }) {
                                throw ClassNotFoundException(className)
                            }
                        } finally {
                            inHook.set(false)
                        }
                    }
                }
            )

            XposedHelpers.findAndHookMethod(
                classLoaderClass,
                "loadClass",
                String::class.java,
                Boolean::class.javaPrimitiveType,
                object : XC_MethodHook() {
                    override fun beforeHookedMethod(param: MethodHookParam) {
                        if (inHook.get() == true) return
                        inHook.set(true)
                        try {
                            val className = param.args[0] as? String ?: return
                            if (xposedClassNames.any { className.contains(it) }) {
                                throw ClassNotFoundException(className)
                            }
                        } finally {
                            inHook.set(false)
                        }
                    }
                }
            )

            Logger.d(Logger.HOOK, "ClassLoader hook installed")

        } catch (e: Throwable) {
            Logger.e("ClassLoader hook failed", e)
        }
    }

    private fun getPackageNameFromItem(item: Any): String {
        return try {
            val field = item.javaClass.getField("packageName")
            field.get(item) as? String ?: ""
        } catch (e: Throwable) {
            ""
        }
    }
}

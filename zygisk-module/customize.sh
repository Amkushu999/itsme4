#!/system/bin/sh
# customize.sh — Magisk module installer script.

SKIP_MOUNT=false
PROPFILE=false
POSTFSDATA=false
LATESTARTSERVICE=true

print_modname() {
    ui_print "─────────────────────────────────────────"
    ui_print "   Amkush Virtual Camera (Native Layer 4)"
    ui_print "   Version: $MODVER"
    ui_print "─────────────────────────────────────────"
}

on_install() {
    ui_print "- Installing to $MODPATH"

    unzip -o "$ZIPFILE" 'system/*'  -d "$MODPATH" >&2
    unzip -o "$ZIPFILE" 'zygisk/*'  -d "$MODPATH" >&2
    unzip -o "$ZIPFILE" 'service.sh' -d "$MODPATH" >&2

    # Verify Zygisk is enabled.
    if [ "$(magisk --sqlite "SELECT value FROM settings WHERE key='zygisk';")" != "value=1" ] 2>/dev/null; then
        ui_print ""
        ui_print "! WARNING: Zygisk appears to be DISABLED in Magisk settings."
        ui_print "! Enable Zygisk and re-flash this module or it will have no effect."
        ui_print ""
    fi

    local api_level=$(getprop ro.build.version.sdk)
    if [ "$api_level" -lt 28 ]; then
        ui_print "! ERROR: Android API $api_level < 28 — this module requires Android 9+."
        abort "Unsupported Android version"
    fi
    ui_print "- Android API $api_level ✓"

    set_perm_recursive "$MODPATH" root root 0755 0644
    set_perm "$MODPATH/service.sh" root root 0755

    ui_print "- Installation complete. Reboot to activate."
}

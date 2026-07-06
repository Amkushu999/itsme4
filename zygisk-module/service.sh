#!/system/bin/sh
# service.sh — Magisk late_start service context (runs after boot).
#
# Responsibilities:
#   1. Patch SELinux policy to allow Zygisk to inject into cameraserver.
#   2. Ensure the hook's Unix domain socket can be accessed by the app.
#   3. Verify the hook is loaded into cameraserver.

MODDIR="${0%/*}"
LOG="$MODDIR/service.log"

log() { echo "[$(date +'%H:%M:%S')] $*" >> "$LOG"; }

# Rotate log — keep last 200 lines.
[ -f "$LOG" ] && tail -200 "$LOG" > "$LOG.tmp" && mv "$LOG.tmp" "$LOG"

log "service.sh starting"

# ── 1. SELinux policy patches ─────────────────────────────────────────────────
SUPOLICY=$(which supolicy 2>/dev/null || echo "magiskpolicy")

if [ -x "$SUPOLICY" ] || which "$SUPOLICY" >/dev/null 2>&1; then
    "$SUPOLICY" --live \
        "allow zygote cameraserver process { ptrace signal }" \
        "allow zygote cameraserver fd { use }" \
        "allow cameraserver app_data_file dir { search }" \
        "allow cameraserver unlabeled { read open }" \
        "allow cameraserver cameraserver_tmpfs { read write execute }" \
        "allow cameraserver zygote_tmpfs { read write execute map }" \
        "allow untrusted_app cameraserver unix_stream_socket { connectto }" \
        "allow cameraserver untrusted_app unix_stream_socket { read write accept }" \
        >> "$LOG" 2>&1
    log "SELinux policies applied"
else
    log "WARNING: supolicy / magiskpolicy not found — SELinux not patched"
fi

# ── 2. Verify Zygisk caught cameraserver ──────────────────────────────────────
sleep 5

CS_PID=$(pgrep cameraserver 2>/dev/null)
if [ -z "$CS_PID" ]; then
    log "cameraserver not running yet — waiting 10s"
    sleep 10
    CS_PID=$(pgrep cameraserver)
fi

if [ -n "$CS_PID" ]; then
    if grep -q "hookProxy" /proc/$CS_PID/maps 2>/dev/null; then
        log "hookProxy.so confirmed in cameraserver (PID=$CS_PID) — OK"
    else
        log "WARNING: hookProxy.so NOT found in cameraserver maps — hook may be inactive"
        log "         Ensure Zygisk is enabled and this module is activated in Magisk."
    fi
else
    log "ERROR: cameraserver process not found"
fi

log "service.sh done"

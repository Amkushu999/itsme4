#!/system/bin/sh
MODDIR="${0%/*}"
LOG="$MODDIR/facegate/service.log"

mkdir -p "$MODDIR/facegate"
log() { echo "[$(date +%H:%M:%S)] $*" >> "$LOG"; }

[ -f "$LOG" ] && tail -300 "$LOG" > "$LOG.tmp" && mv "$LOG.tmp" "$LOG"
log "service.sh start — module at $MODDIR"

HOOK64="/system/lib64/libhookProxy.so"
HOOK32="/system/lib/libhookProxy.so"

if [ -f "$HOOK64" ]; then
    resetprop wrap.cameraserver "LD_PRELOAD $HOOK64"
    log "wrap.cameraserver → LD_PRELOAD $HOOK64"
elif [ -f "$HOOK32" ]; then
    resetprop wrap.cameraserver "LD_PRELOAD $HOOK32"
    log "wrap.cameraserver → LD_PRELOAD $HOOK32"
else
    log "ERROR: hookProxy.so not found — LD_PRELOAD not set"
    exit 0
fi

CS_PID=$(pgrep -f cameraserver 2>/dev/null | head -1)
if [ -z "$CS_PID" ]; then
    log "WARNING: cameraserver not running yet during early service state"
else
    if grep -q "hookProxy" /proc/$CS_PID/maps 2>/dev/null; then
        log "hookProxy.so confirmed in cameraserver PID=$CS_PID"
    else
        log "WARNING: hookProxy.so NOT in maps yet — check back after full boot completes"
    fi
fi
log "service.sh done"

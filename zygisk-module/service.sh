#!/system/bin/sh
  # service.sh — Magisk late_start service context.
  #
  # HOW SHADOWHOOK LOADS INTO CAMERASERVER:
  #   cameraserver is started by Android init (not Zygote).
  #   Zygisk preAppSpecialize does NOT fire for it.
  #   Instead, Android's "wrap.NAME" property tells init to inject LD_PRELOAD:
  #
  #       resetprop wrap.cameraserver "LD_PRELOAD /system/lib64/libhookProxy.so"
  #
  #   On the next init start of cameraserver (after reboot or stop/start),
  #   libhookProxy.so is loaded first. Its __attribute__((constructor)) detects
  #   "cameraserver" in /proc/self/cmdline and calls hook_proxy_install() which
  #   uses ShadowHook to patch Camera3Device::processCaptureResult in-place.

  MODDIR="${0%/*}"
  LOG="$MODDIR/facegate/service.log"

  mkdir -p "$MODDIR/facegate"

  log() { echo "[$(date +%H:%M:%S)] $*" >> "$LOG"; }

  # Keep last 300 lines
  [ -f "$LOG" ] && tail -300 "$LOG" > "$LOG.tmp" && mv "$LOG.tmp" "$LOG"

  log "service.sh start — module at $MODDIR"

  # ── SELinux patches ───────────────────────────────────────────────────────────
  for BIN in supolicy magiskpolicy; do
      which $BIN >/dev/null 2>&1 && SUPOL=$BIN && break
  done
  if [ -n "$SUPOL" ]; then
      $SUPOL --live \
          "allow cameraserver cameraserver_tmpfs { read write execute map }" \
          "allow cameraserver unlabeled { read open getattr }" \
          "allow init cameraserver process { noatsecure }" \
          "allow untrusted_app cameraserver unix_stream_socket { connectto }" \
          "allow cameraserver untrusted_app unix_stream_socket { read write accept }" \
          >> "$LOG" 2>&1
      log "SELinux patches applied"
  else
      log "WARNING: supolicy not found — SELinux not patched"
  fi

  # ── Set wrap.cameraserver so LD_PRELOAD fires on next init start ──────────────
  # Magisk system overlay mounts libhookProxy.so at /system/lib64/ (arm64)
  # and /system/lib/ (arm) before init starts any services after reboot.
  HOOK64="/system/lib64/libhookProxy.so"
  HOOK32="/system/lib/libhookProxy.so"

  if [ -f "$HOOK64" ]; then
      resetprop wrap.cameraserver "LD_PRELOAD $HOOK64"
      log "wrap.cameraserver → LD_PRELOAD $HOOK64"
  elif [ -f "$HOOK32" ]; then
      resetprop wrap.cameraserver "LD_PRELOAD $HOOK32"
      log "wrap.cameraserver → LD_PRELOAD $HOOK32"
  else
      log "ERROR: hookProxy.so not in system overlay — LD_PRELOAD not set"
      log "       Run the builder workflow to produce a module ZIP with system/lib64/"
      exit 0
  fi

  # ── Restart cameraserver so wrap property takes effect ───────────────────────
  log "Stopping cameraserver..."
  stop cameraserver
  sleep 2
  log "Starting cameraserver with wrap.cameraserver in effect..."
  start cameraserver

  # ── Verify hookProxy loaded ───────────────────────────────────────────────────
  sleep 10
  CS_PID=$(pgrep -f cameraserver 2>/dev/null | head -1)
  if [ -z "$CS_PID" ]; then
      log "ERROR: cameraserver not running after restart"
  else
      if grep -q "hookProxy" /proc/$CS_PID/maps 2>/dev/null; then
          log "hookProxy.so confirmed in cameraserver PID=$CS_PID"
      else
          log "WARNING: hookProxy.so NOT in cameraserver maps — wrap may be blocked by SELinux"
      fi
  fi

  log "service.sh done"
  
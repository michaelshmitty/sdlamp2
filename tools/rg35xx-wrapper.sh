#!/bin/sh
#
# RG35XX Plus wrapper for sdlamp2
#
# Launched by dmenu_ln instead of sdlamp2 directly. Handles device-specific
# concerns that don't belong in the player binary:
#   1. Monitor power button and trigger clean shutdown
#   2. Launch sdlamp2 as the main process
#
# Install: copy to /mnt/vendor/bin/rg35xx-wrapper.sh
# Config:  set CMD in dmenu_ln to point here instead of sdlamp2 directly

SDLAMP2="/mnt/vendor/bin/sdlamp2"
AUDIO_DIR="/mnt/sdcard/Music"

# --- WiFi hotspot ---
# Deprioritized: connecting the device as a WiFi client to a shared network
# works fine even when sdlamp2 replaces the stock menu. Hotspot/AP mode isn't
# needed — SSH access works over the shared network.

# --- Power button monitor ---
# axp2202-pek on /dev/input/event0 sends KEY_POWER (code 116).
# logind has HandlePowerKey=ignore, so we handle it here.
POWER_EVENT_DEV="/dev/input/event0"

monitor_power_button() {
  # evtest writes one line per event; filter for KEY_POWER press (value 1)
  evtest "$POWER_EVENT_DEV" 2>/dev/null | while read -r line; do
    case "$line" in
      *"code 116"*"value 1"*)
        kill -TERM "$SDLAMP2_PID" 2>/dev/null
        sleep 1
        poweroff
        ;;
    esac
  done
}

# --- Launch sdlamp2 ---
# Run in background so we can capture PID for the power button monitor.
"$SDLAMP2" "$AUDIO_DIR" &
SDLAMP2_PID=$!

monitor_power_button &
MONITOR_PID=$!

# Wait for sdlamp2 to finish (signal or normal exit).
wait "$SDLAMP2_PID"
SDLAMP2_EXIT=$?

# --- Cleanup ---
# Kill the power button monitor if it's still running
kill "$MONITOR_PID" 2>/dev/null

exit "$SDLAMP2_EXIT"

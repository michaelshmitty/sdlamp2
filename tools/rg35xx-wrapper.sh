#!/bin/sh
#
# RG35XX Plus wrapper for sdlamp2
#
# Launched by dmenu_ln instead of sdlamp2 directly. Handles device-specific
# concerns that don't belong in the player binary:
#   1. Monitor power button and trigger clean shutdown
#   2. Display the stock firmware's shutdown screen (goodbye.png → /dev/fb0)
#   3. Launch sdlamp2 as the main process
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
  POWER_TIMER_PID=""
  evtest "$POWER_EVENT_DEV" 2>/dev/null | while read -r line; do
    case "$line" in
      *"code 116"*"value 1"*)
        # Power button pressed — start 3-second hold timer
        ( trap 'exit 0' TERM; sleep 3; touch /tmp/.sdlamp2_shutdown; kill -TERM "$SDLAMP2_PID" 2>/dev/null ) &
        POWER_TIMER_PID=$!
        ;;
      *"code 116"*"value 0"*)
        # Power button released — cancel timer if still running
        if [ -n "$POWER_TIMER_PID" ]; then
          kill "$POWER_TIMER_PID" 2>/dev/null
          wait "$POWER_TIMER_PID" 2>/dev/null
          POWER_TIMER_PID=""
        fi
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

# If this was a shutdown, call poweroff and block so the loadapp.sh restart
# loop doesn't relaunch dmenu_ln (which would take over the framebuffer and
# overwrite sdlamp2's shutdown screen).
if [ -f /tmp/.sdlamp2_shutdown ]; then
  rm -f /tmp/.sdlamp2_shutdown
  # Display the stock firmware's shutdown screen via framebuffer.
  # goodbye.png is 640x480 RGB — exactly matches the display.
  # /dev/fb0 is 32bpp BGRA, so we swap R/B channels and write raw pixels.
  python3 -c "
from PIL import Image
img = Image.open('/mnt/vendor/res1/shutdown/goodbye.png').convert('RGBA')
r, g, b, a = img.split()
with open('/dev/fb0', 'wb') as f:
    f.write(Image.merge('RGBA', (b, g, r, a)).tobytes())
" 2>/dev/null
  poweroff
  sleep 30
fi

exit "$SDLAMP2_EXIT"

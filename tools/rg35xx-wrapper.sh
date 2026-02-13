#!/bin/sh
#
# RG35XX Plus wrapper for sdlamp2
#
# Launched by dmenu_ln instead of sdlamp2 directly. Handles device-specific
# concerns that don't belong in the player binary:
#   1. Screen idle timeout, power button screen toggle, and long-press shutdown
#   2. Display the stock firmware's shutdown screen (goodbye.png → /dev/fb0)
#   3. Launch sdlamp2 as the main process
#
# Install: copy to /mnt/vendor/bin/rg35xx-wrapper.sh
# Config:  set CMD in dmenu_ln to point here instead of sdlamp2 directly

SDLAMP2="/mnt/vendor/bin/sdlamp2"
AUDIO_DIR="/mnt/sdcard/Music"
SCREEN_MONITOR="/mnt/vendor/bin/rg35xx-screen-monitor.py"

# --- WiFi hotspot ---
# Deprioritized: connecting the device as a WiFi client to a shared network
# works fine even when sdlamp2 replaces the stock menu. Hotspot/AP mode isn't
# needed — SSH access works over the shared network.

# --- Launch sdlamp2 ---
"$SDLAMP2" "$AUDIO_DIR" &
SDLAMP2_PID=$!

# --- Screen monitor (idle timeout + power button toggle + long-press shutdown) ---
# Must launch after sdlamp2 so PID is available. Reads /dev/input/event0
# directly for power button events — no evtest dependency.
python3 "$SCREEN_MONITOR" "$SDLAMP2_PID" &
SCREEN_MONITOR_PID=$!

# Wait for sdlamp2 to finish (signal or normal exit).
wait "$SDLAMP2_PID"
SDLAMP2_EXIT=$?

# --- Cleanup ---
# Kill the screen monitor (SIGTERM restores brightness).
kill "$SCREEN_MONITOR_PID" 2>/dev/null
wait "$SCREEN_MONITOR_PID" 2>/dev/null

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

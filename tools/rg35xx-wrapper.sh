#!/bin/sh
#
# RG35XX Plus wrapper for sdlamp2
#
# Launched by dmenu_ln instead of sdlamp2 directly. Handles device-specific
# concerns that don't belong in the player binary:
#   1. Start WiFi hotspot (AP mode for SSH access)
#   2. Launch sdlamp2 as the foreground process
#   3. Monitor power button and trigger clean shutdown
#
# Install: copy to /mnt/vendor/bin/rg35xx-wrapper.sh
# Config:  set CMD in dmenu_ln to point here instead of sdlamp2 directly

SDLAMP2="/mnt/vendor/bin/sdlamp2"
AUDIO_DIR="/mnt/sdcard/Music"

# --- WiFi hotspot ---
# TODO: Investigate how the stock menu starts AP mode.
# Likely candidates: hostapd, nmcli, or a vendor script in /mnt/vendor/ctrl/.
# Run these on the device to find out:
#   grep -r 'hostapd\|hotspot\|ap_mode\|wifi' /mnt/vendor/ctrl/
#   systemctl list-units | grep -i net
#   nmcli general status && nmcli connection show
#
# Placeholder — uncomment/replace once the mechanism is known:
# nmcli connection up hotspot 2>/dev/null || true

# --- Power button monitor ---
# TODO: Investigate power button input device.
# Run on device:
#   cat /proc/bus/input/devices   (find the power button event device)
#   evtest /dev/input/eventN      (confirm KEY_POWER event code)
#   cat /etc/systemd/logind.conf  (check HandlePowerKey setting)
#
# Strategy: read key events from the power button input device in background.
# When KEY_POWER (code 116) is detected, send SIGTERM to sdlamp2 and poweroff.
#
# POWER_EVENT_DEV="/dev/input/eventN"  # TBD: set after investigation
#
# monitor_power_button() {
#   # evtest writes one line per event; filter for KEY_POWER press (value 1)
#   evtest "$POWER_EVENT_DEV" 2>/dev/null | while read -r line; do
#     case "$line" in
#       *"code 116"*"value 1"*)
#         kill -TERM "$SDLAMP2_PID" 2>/dev/null
#         sleep 1
#         poweroff
#         ;;
#     esac
#   done
# }
# monitor_power_button &
# MONITOR_PID=$!

# --- Launch sdlamp2 ---
"$SDLAMP2" "$AUDIO_DIR"
SDLAMP2_EXIT=$?

# --- Cleanup ---
# Kill the power button monitor if it's still running
# kill "$MONITOR_PID" 2>/dev/null

exit "$SDLAMP2_EXIT"

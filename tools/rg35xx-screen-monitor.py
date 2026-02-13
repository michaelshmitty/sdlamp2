#!/usr/bin/env python3
"""
Screen idle timeout, power button toggle, and long-press shutdown for RG35XX Plus.

Monitors /dev/input/event{0,1,2} for activity. Turns off the screen
(via Allwinner /dev/disp SET_BRIGHTNESS ioctl) after 15s of no input.
Any button press wakes the screen. A short power button press (<2s)
toggles the screen on/off. A long press (3s+) triggers clean shutdown.

Launched by rg35xx-wrapper.sh alongside sdlamp2. Killed on cleanup.

Usage: rg35xx-screen-monitor.py <sdlamp2_pid>
"""
import fcntl
import os
import select
import signal
import struct
import sys
import time

IDLE_TIMEOUT = 15  # seconds
POWER_SHORT_THRESHOLD = 2.0  # seconds — short press if released before this
POWER_LONG_THRESHOLD = 3.0  # seconds — shutdown if held this long

# Allwinner /dev/disp ioctl commands
DISP_GET_BRIGHTNESS = 0x103
DISP_SET_BRIGHTNESS = 0x102

# Input event constants
EV_KEY = 1
KEY_POWER = 116

# struct input_event on aarch64: struct timeval (2x long=8 bytes each) + __u16 type + __u16 code + __s32 value
INPUT_EVENT_FORMAT = "@llHHi"
INPUT_EVENT_SIZE = struct.calcsize(INPUT_EVENT_FORMAT)

EVENT_DEVICES = ["/dev/input/event0", "/dev/input/event1", "/dev/input/event2"]


def disp_ioctl(fd, cmd, screen=0, value=0):
    # Allwinner disp ioctls take an array of 4 unsigned longs as arg
    args = struct.pack("@4L", screen, value, 0, 0)
    result = fcntl.ioctl(fd, cmd, args)
    return struct.unpack("@4L", result)[0]


def get_brightness(disp_fd):
    return disp_ioctl(disp_fd, DISP_GET_BRIGHTNESS)


def set_brightness(disp_fd, value):
    disp_ioctl(disp_fd, DISP_SET_BRIGHTNESS, value=value)


def main():
    if len(sys.argv) < 2:
        print("Usage: rg35xx-screen-monitor.py <sdlamp2_pid>", file=sys.stderr)
        sys.exit(1)

    sdlamp2_pid = int(sys.argv[1])

    disp_fd = os.open("/dev/disp", os.O_RDWR)
    original_brightness = get_brightness(disp_fd)
    if original_brightness == 0:
        original_brightness = 50  # sensible default if already off

    screen_on = True
    power_press_time = None

    # Restore brightness on exit so goodbye.png is visible during shutdown
    def restore_and_exit(signum, frame):
        if not screen_on:
            set_brightness(disp_fd, original_brightness)
        os.close(disp_fd)
        sys.exit(0)

    signal.signal(signal.SIGTERM, restore_and_exit)
    signal.signal(signal.SIGINT, restore_and_exit)

    # Open input devices (non-blocking)
    event_fds = []
    for path in EVENT_DEVICES:
        try:
            fd = os.open(path, os.O_RDONLY | os.O_NONBLOCK)
            event_fds.append(fd)
        except OSError as e:
            print(f"screen-monitor: cannot open {path}: {e}", file=sys.stderr)

    if not event_fds:
        print("screen-monitor: no input devices available, exiting", file=sys.stderr)
        os.close(disp_fd)
        sys.exit(1)

    while True:
        # Dynamic timeout: if power button is held, shorten timeout to detect 3s mark
        timeout = IDLE_TIMEOUT
        if power_press_time is not None:
            remaining = POWER_LONG_THRESHOLD - (time.monotonic() - power_press_time)
            if remaining <= 0:
                # Already past threshold — trigger shutdown now
                touch_and_shutdown(disp_fd, original_brightness, screen_on, sdlamp2_pid)
                return
            timeout = min(timeout, remaining)

        readable, _, _ = select.select(event_fds, [], [], timeout)

        # Check long-press threshold (whether select returned due to timeout or input)
        if power_press_time is not None:
            held = time.monotonic() - power_press_time
            if held >= POWER_LONG_THRESHOLD:
                touch_and_shutdown(disp_fd, original_brightness, screen_on, sdlamp2_pid)
                return

        if not readable:
            # Timeout with no input — turn off screen if idle
            if screen_on and power_press_time is None:
                set_brightness(disp_fd, 0)
                screen_on = False
            continue

        # Process input events from all readable fds
        for fd in readable:
            while True:
                try:
                    data = os.read(fd, INPUT_EVENT_SIZE)
                except BlockingIOError:
                    break
                if len(data) < INPUT_EVENT_SIZE:
                    break

                _sec, _usec, ev_type, ev_code, ev_value = struct.unpack(
                    INPUT_EVENT_FORMAT, data
                )

                if ev_type != EV_KEY:
                    continue

                # Power button handling
                if ev_code == KEY_POWER:
                    if ev_value == 1:  # press
                        power_press_time = time.monotonic()
                    elif ev_value == 0 and power_press_time is not None:  # release
                        hold_duration = time.monotonic() - power_press_time
                        power_press_time = None
                        if hold_duration < POWER_SHORT_THRESHOLD:
                            # Short press — toggle screen
                            if screen_on:
                                set_brightness(disp_fd, 0)
                                screen_on = False
                            else:
                                set_brightness(disp_fd, original_brightness)
                                screen_on = True
                        # Between SHORT and LONG threshold: ignore (release before 3s)
                    continue

                # Any other key press — wake screen if off
                if ev_value == 1 and not screen_on:
                    set_brightness(disp_fd, original_brightness)
                    screen_on = True


def touch_and_shutdown(disp_fd, original_brightness, screen_on, sdlamp2_pid):
    """Signal sdlamp2 to exit and flag for shutdown."""
    if not screen_on:
        set_brightness(disp_fd, original_brightness)
    os.close(disp_fd)
    try:
        open("/tmp/.sdlamp2_shutdown", "w").close()
    except OSError:
        pass
    try:
        os.kill(sdlamp2_pid, signal.SIGTERM)
    except OSError:
        pass


if __name__ == "__main__":
    main()

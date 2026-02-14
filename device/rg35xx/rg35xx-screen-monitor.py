#!/usr/bin/env python3
"""
Screen idle timeout, power button toggle, and long-press shutdown for RG35XX Plus.

Monitors /dev/input/event{0,1,2} for activity. Turns off the screen
(via Allwinner /dev/disp SET_BRIGHTNESS ioctl) after 15s of no input.
Any input event (keys, d-pad, joystick) wakes the screen. While the
screen is off, inputs are grabbed (EVIOCGRAB) so SDL in sdlamp2 does
not receive the wake event.

A short power button press (<2s) toggles the screen on/off. A long
press (3s+) triggers clean shutdown. If the device is idle (no input
and no audio playback) for 10 minutes, it auto-shuts down to save
battery.

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

IDLE_TIMEOUT = 15  # seconds — screen off after no input
IDLE_SHUTDOWN_TIMEOUT = 600  # seconds — auto-shutdown after no input AND no playback
POWER_SHORT_THRESHOLD = 2.0  # seconds — short press if released before this
POWER_LONG_THRESHOLD = 3.0  # seconds — shutdown if held this long

# Allwinner /dev/disp ioctl commands
DISP_GET_BRIGHTNESS = 0x103
DISP_SET_BRIGHTNESS = 0x102

# Input event constants
EV_SYN = 0
EV_KEY = 1
KEY_POWER = 116

# EVIOCGRAB — exclusive access to input device (_IOW('E', 0x90, int))
EVIOCGRAB = 0x40044590

AUDIO_EXTENSIONS = ('.m4a', '.mp3', '.wav', '.ogg')

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


def grab_inputs(event_fds, grab):
    """Grab or release exclusive access to input devices."""
    for fd in event_fds:
        try:
            fcntl.ioctl(fd, EVIOCGRAB, 1 if grab else 0)
        except OSError:
            pass


def get_audio_file_pos(pid):
    """Return the file offset of sdlamp2's open audio file, or None."""
    fd_dir = f"/proc/{pid}/fd"
    try:
        for fd_name in os.listdir(fd_dir):
            try:
                target = os.readlink(f"{fd_dir}/{fd_name}")
                if any(target.endswith(ext) for ext in AUDIO_EXTENSIONS):
                    with open(f"/proc/{pid}/fdinfo/{fd_name}") as f:
                        for line in f:
                            if line.startswith("pos:"):
                                return int(line.split()[1])
            except OSError:
                continue
    except OSError:
        pass
    return None


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

    # Restore brightness and release grabs on exit so goodbye.png is visible during shutdown
    def restore_and_exit(signum, frame):
        grab_inputs(event_fds, False)
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

    # Idle auto-shutdown state
    last_active_time = time.monotonic()
    last_audio_pos = get_audio_file_pos(sdlamp2_pid)

    while True:
        # Dynamic timeout: if power button is held, shorten timeout to detect 3s mark
        timeout = IDLE_TIMEOUT
        if power_press_time is not None:
            remaining = POWER_LONG_THRESHOLD - (time.monotonic() - power_press_time)
            if remaining <= 0:
                # Already past threshold — trigger shutdown now
                grab_inputs(event_fds, False)
                touch_and_shutdown(disp_fd, original_brightness, screen_on, sdlamp2_pid)
                return
            timeout = min(timeout, remaining)

        readable, _, _ = select.select(event_fds, [], [], timeout)

        # Check long-press threshold (whether select returned due to timeout or input)
        if power_press_time is not None:
            held = time.monotonic() - power_press_time
            if held >= POWER_LONG_THRESHOLD:
                grab_inputs(event_fds, False)
                touch_and_shutdown(disp_fd, original_brightness, screen_on, sdlamp2_pid)
                return

        if not readable:
            # Timeout with no input — turn off screen if idle
            if screen_on and power_press_time is None:
                set_brightness(disp_fd, 0)
                screen_on = False
                grab_inputs(event_fds, True)

            # Check audio playback activity for idle auto-shutdown
            audio_pos = get_audio_file_pos(sdlamp2_pid)
            if audio_pos is not None and audio_pos != last_audio_pos:
                last_active_time = time.monotonic()
            last_audio_pos = audio_pos

            # Auto-shutdown if idle long enough (no input + no playback)
            if time.monotonic() - last_active_time >= IDLE_SHUTDOWN_TIMEOUT:
                grab_inputs(event_fds, False)
                touch_and_shutdown(disp_fd, original_brightness, screen_on, sdlamp2_pid)
                return
            continue

        # Process input events from all readable fds
        any_activity = False
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

                if ev_type == EV_SYN:
                    continue

                # Power button handling (EV_KEY only)
                if ev_type == EV_KEY and ev_code == KEY_POWER:
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
                                grab_inputs(event_fds, True)
                            else:
                                grab_inputs(event_fds, False)
                                set_brightness(disp_fd, original_brightness)
                                screen_on = True
                        # Between SHORT and LONG threshold: ignore (release before 3s)
                    continue

                any_activity = True

        # Any input activity resets idle shutdown timer
        if any_activity:
            last_active_time = time.monotonic()

        # Any activity wakes screen (d-pad, face buttons, etc.)
        if any_activity and not screen_on:
            grab_inputs(event_fds, False)
            set_brightness(disp_fd, original_brightness)
            screen_on = True


def touch_and_shutdown(disp_fd, original_brightness, screen_on, sdlamp2_pid):
    """Signal sdlamp2 to exit and flag for shutdown.

    Caller must release EVIOCGRAB before calling this.
    """
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

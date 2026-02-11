# SDLamp2 - Functional Specification Document

## Document Information

| Field   | Value      |
| :------ | :--------- |
| Version | 1.0        |
| Status  | Draft      |
| Created | 2026-02-10 |
| Updated | 2026-02-10 |

## 1. Purpose

This document specifies the functional requirements for an SDL2 based media player application written in C, aimed at being used by a pre-teen child. The name is a reference to the Winamp media player, popular in the 90's and inspiration can be drawn from that.

## 2. Goals

- Play large m4a audio files, approximately 3 hours each, created from original cassette tapes that contain collections of fairy tales for children
- Present a super simple interface, easy to use by a child less than 10 years old, reminiscent of a cassette player: rewind, stop, play / pause, fast forward, load another tape
- The playback of the audio files must emulate that of cassette tapes, meaning the position of each file should be stored and remembered and playback should resume from that position even if other files have been played in the mean time
- The interface should show the embedded album cover if present in the file

## 3. Software architecture

- The program should be written in modern C using GCC or Clang
- It should use the SDL2 library for screen rendering, audio playback and input handling
- It should use the libav (ffmpeg) suite of libraries to decode m4a files and potentially other formats (e.g. mp3)
- It should not call out to an ffmpeg binary, but instead use the libav C API library functions

## 4. Design principles

- Version control (git) must be used
- Compilation should be performed by a simple shell script or batch file, not a complicated build system like make or cmake
- C source code files should be formatted using "Google" style with an additional change of `ColumnLimit` set to 100
- Less is more, minimize dependencies, avoid pulling in extra libraries, always talk through with owner first
- Keep it simple, apply Casey Muratori's `semantic compression` principles, don't refactor too soon or write code that's too clever for its own good
- Keep a changelog in this functional specification document

## 5. Target Platforms

- **Deployment**: Linux ARM64 (aarch64) on a retro handheld gaming device
- **Development/testing**: macOS Apple Silicon

## 6. Changelog

### 2026-02-11 — Debug flag for input diagnostics

- **`--debug` flag**: New command-line option (`./sdlamp2 --debug [audio_dir]`) that enables verbose logging of all SDL input events to stdout. Designed to diagnose controller issues on retro handheld devices where non-standard controllers may not be recognized by SDL's GameController API.
- **Startup enumeration**: When debug is on, logs the number of detected joysticks, each joystick's name, whether SDL recognizes it as a GameController, and its GUID.
- **Event logging**: Logs `KEYDOWN/UP`, `CONTROLLERBUTTONDOWN/UP`, `JOYBUTTONDOWN/UP`, `JOYHATMOTION`, `JOYAXISMOTION` (with deadzone filter), and device add/remove events with identifying details.

### 2026-02-11 — Lossless M4A concatenation tool

- **New tool**: `tools/concat_cassette.sh` — losslessly concatenates multiple m4a files into a single file using the ffmpeg concat demuxer (`-c copy`, no re-encoding). Designed to recombine individual story files back into per-cassette files.
- **Chapter markers**: Generates ffmpeg metadata with one chapter per input file, titled from the input filename (sans extension). Chapters enable navigation within the combined file.
- **Album art preserved**: Attached pictures from the first input file carry through automatically via stream copy.
- **Fast-start output**: Uses `-movflags +faststart` to place the moov atom at the front for better seeking.

### 2026-02-10 — Volume control and d-pad/keyboard navigation

- **Cursor-based navigation**: Replaced mouse input with a focus-highlight model. Arrow keys (Left/Right) and d-pad move a visible blue highlight between UI elements; Enter/A button activates the focused button. Designed for use on a handheld gaming device with no mouse.
- **App-level volume control**: Float samples are scaled by a volume factor (0–100%) before being queued to the audio device. Volume slider rendered as a vertical bar to the left of the transport buttons, using the same gray palette as the progress bar.
- **Volume persistence**: Volume level saved to `volume.txt` in the audio directory. Defaults to 50% on first run. Loaded on startup, saved on quit and on every adjustment.
- **SDL_GameController support**: Uses `SDL_INIT_GAMECONTROLLER` and the `SDL_GameController` API to normalize d-pad input across hardware. Hot-plug support via `SDL_CONTROLLERDEVICEADDED/REMOVED`. Keyboard (arrow keys + Enter) works identically for desktop testing.
- **Removed mouse input**: `SDL_MOUSEBUTTONDOWN` handler removed entirely; all interaction is now via keyboard or gamepad.

### 2026-02-10 — Full implementation of audio player features

- **Streaming decoder**: Replaced fire-and-forget `decode_audio()` with a persistent `Decoder` struct that streams audio on demand via `decoder_pump()`. Uses `libswresample` (`swr_alloc_set_opts2`) to convert from the decoder's native format (e.g. planar float) to interleaved float stereo 48kHz. Fixes the sped-up/distorted audio bug and eliminates the multi-GB memory spike for long files.
- **Seeking**: Rewind (10s back) and fast-forward (10s ahead) via `av_seek_frame()` with codec buffer flush and audio pipeline clear. Clamped to file bounds.
- **Play/Stop separation**: Removed play/pause toggle. Play always resumes, stop always pauses in place and saves position. No icon toggling.
- **Position persistence**: Saves/loads playback position per file in `positions.txt` (tab-separated) in the audio directory. Position saved on stop, quit, and file switch. Restored on file open.
- **File selection**: Scans audio directory for `.m4a`, `.mp3`, `.wav`, `.ogg` files. Sorted alphabetically. 5th button ("next tape") cycles through files. Window title shows current filename.
- **Album art**: Extracts embedded cover art (`AV_DISPOSITION_ATTACHED_PIC`) and displays it scaled with preserved aspect ratio in the upper portion of the window.
- **Progress bar**: Gray bar between album art and controls showing playback position relative to duration.
- **Command-line argument**: First argument sets audio directory (defaults to current working directory).
- **Error handling**: Non-fatal errors (stream ops, corrupt files) use `fprintf(stderr)` and continue. Corrupt files are skipped when switching. Fatal errors (SDL init, window, audio device) still abort. Proper cleanup order on exit.
- **EOF handling**: When a file plays to the end, playback auto-pauses and resets to the start.
- **Removed dead code**: `load_audio_file()`, `wavbuf`/`wavlen`/`wavspec` globals.

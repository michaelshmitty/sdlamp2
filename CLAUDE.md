# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

SDLamp2 is a simple SDL2-based audio player written in C, designed for a child to use. It plays long m4a/mp3 files (fairy tale cassette rips) with a cassette-player-like UI: rewind, stop, play, fast-forward, next tape. Inspired by Winamp.

See `docs/sdlamp2-fsd.md` for the full functional specification and changelog.

## Build and Run

**Dependencies:** SDL2, SDL2_image, FFmpeg libraries (libavformat, libavcodec, libavutil, libswresample). Installed via system package manager; resolved at build time with `sdl2-config` and `pkgconf`.

```sh
./build.sh              # macOS — builds to build/sdlamp2
./build_aarch64.sh      # Linux aarch64 variant
cd build && ./sdlamp2 [audio_directory]
```

The binary must be run from `build/` because it loads `controls.png` from the current working directory. Audio directory defaults to cwd if not specified.

No test suite, no linter, no Makefile/CMake.

## Architecture

Single-file C program: `src/sdlamp2.c` (~650 lines). No headers, no separate compilation units.

Key sections in order:
- **Decoder struct** — holds all FFmpeg state (format/codec contexts, swr resampler, album art texture)
- **Position persistence** — `save_position()`/`load_position()` read/write a tab-separated `positions.txt` in the audio directory
- **File scanning** — `scan_audio_files()` finds supported files (.m4a, .mp3, .wav, .ogg) in a directory
- **Decoder lifecycle** — `decoder_open()` sets up FFmpeg + swr pipeline (planar float → interleaved stereo 48kHz), `decoder_pump()` feeds decoded audio into an SDL_AudioStream, `decoder_seek()` handles seeking
- **File switching** — `switch_file()` saves position, opens new file, restores saved position
- **Main loop** — event handling (mouse clicks on button rects), decoder pumping, SDL audio queue draining, EOF handling, rendering (album art, progress bar, control buttons from sprite sheet)

Audio pipeline: FFmpeg decoder → libswresample (format conversion) → SDL_AudioStream (FIFO) → SDL audio device queue (push model, no callback).

Uses SDL2 (not SDL3). Uses `#if LIBAVUTIL_VERSION_INT` preprocessor checks to support both old (channel_layout) and new (ch_layout) FFmpeg channel layout APIs.

## Conventions

- C formatted with Google style, `ColumnLimit: 100`
- Keep it simple — Casey Muratori's semantic compression; don't refactor too soon
- Minimize dependencies; discuss with owner before adding any
- Non-fatal errors go to stderr and continue; fatal errors (SDL init failures) abort via `panic_and_abort()`
- Update the changelog in `docs/sdlamp2-fsd.md` when making changes

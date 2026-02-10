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

## 5. Changelog

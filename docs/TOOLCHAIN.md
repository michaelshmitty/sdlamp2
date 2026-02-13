# Cross-Compilation Toolchain

This document describes the current Docker/Buildroot cross-compilation toolchain, the architecture mismatch with the target device, and possible approaches to fix it.

## Target Device

### Hardware

- **Device:** Anbernic RG35XX Plus
- **SoC:** Allwinner H700 — quad-core ARM Cortex-A53 @ 1.5 GHz (AArch64 / ARMv8-A)
- **GPU:** Mali G31 MP2
- **RAM:** 1 GB LPDDR4
- **Display:** 640×480 IPS

### Software

- **OS:** Ubuntu 22.04 LTS (Jammy Jellyfish) — [modified stock firmware](https://github.com/cbepx-me/Anbernic-H700-RG-xx-StockOS-Modification)
- **Kernel:** Linux 4.9.170 aarch64 (proprietary, no source released by Anbernic)
- **Architecture:** aarch64 / arm64 (confirmed via `dpkg --print-architecture`)
- **glibc:** 2.35

### Libraries on Device

All required shared libraries are pre-installed. Most are at `/usr/lib/`, some at `/usr/lib/aarch64-linux-gnu/` (Debian multiarch path):

| Library | Soname | Notes |
|---------|--------|-------|
| SDL2 | `libSDL2-2.0.so.0.12.0` | SDL 2.0.12 |
| SDL2_image | `libSDL2_image-2.0.so.0.900.0` | SDL2_image 2.0.9, at multiarch path |
| libavcodec | `libavcodec.so.58` | FFmpeg ~4.x |
| libavformat | `libavformat.so.58` | FFmpeg ~4.x |
| libavutil | `libavutil.so.56` | FFmpeg ~4.x |
| libswresample | `libswresample.so.3` | FFmpeg ~4.x |

### Implications for Cross-Compilation

- The device userland is Debian/Ubuntu-based (has `dpkg`, `apt-get`, `systemd`)
- Shared libraries are already present — no need to bundle or build them via Buildroot
- A native aarch64 compile (e.g. inside an arm64 Docker container) produces working binaries
- Must link against glibc ≤ 2.35 (use GCC ≤ 12 to avoid GLIBCXX version mismatches)

## Current Toolchain Architecture

### Pipeline Overview

The toolchain is built via a Docker container defined in `docker/`:

1. **`docker/Dockerfile`** — Debian Buster (EOL) base image, installs build dependencies (bc, build-essential, cmake, git, etc.), copies support scripts, runs `build-toolchain.sh`, sources `setup-env.sh` in `.bashrc`
2. **`docker/support/build-toolchain.sh`** — Downloads Buildroot 2017.11, applies patches, loads `rg35xx-buildroot-2017.11.config`, runs `make world`. If a pre-built `rg35xx-toolchain.tar.xz` exists, extracts that instead of rebuilding.
3. **`docker/support/install-toolchain.sh`** — Copies host tools from `buildroot/output/host/usr/` to `/opt/rg35xx-toolchain/usr/`, copies `hwcap.h` into the sysroot, runs `relocate-sdk.sh` to fix embedded paths.
4. **`docker/support/setup-env.sh`** — Sets environment variables:
   ```sh
   export PATH="/opt/rg35xx-toolchain/usr/bin:${PATH}:/opt/rg35xx-toolchain/usr/arm-buildroot-linux-gnueabihf/sysroot/bin"
   export CROSS_COMPILE=/opt/rg35xx-toolchain/usr/bin/arm-buildroot-linux-gnueabihf-
   export PREFIX=/opt/rg35xx-toolchain/usr/arm-buildroot-linux-gnueabihf/sysroot/usr
   export UNION_PLATFORM=rg35xx
   ```
5. **`docker/support/package-toolchain.sh`** — Archives `/opt/rg35xx-toolchain` as `rg35xx-toolchain.tar.xz` for caching.
6. **`docker/support/relocate-sdk.sh`** — Rewrites hardcoded paths in SDK text files so the toolchain can live at a different filesystem location.
7. **`docker/Makefile`** — `make shell` builds the Docker image (tag `rg35xx-toolchain`) and drops into an interactive bash shell with `./workspace` mounted at `/root/workspace`.

### How the Main Makefile Uses It

The project `Makefile` detects `PREFIX` to switch modes:

- **Cross-compile** (PREFIX set): uses `-I$(PREFIX)/include` and `-L$(PREFIX)/lib` directly.
- **Native** (no PREFIX): uses `pkg-config` to find SDL2/FFmpeg.

`CROSS_COMPILE` is prepended to `gcc` to select the cross-compiler (e.g. `arm-buildroot-linux-gnueabihf-gcc`).

## The Problem

The Buildroot config targets **32-bit ARM Cortex-A9** but the Anbernic RG35XX runs a **64-bit aarch64 userland**.

The cross-compiled binary is `ELF 32-bit LSB executable, ARM, EABI5` while the device only has 64-bit aarch64 shared libraries. The binary cannot run — the dynamic linker and every `.so` it needs are the wrong architecture.

Key mismatched settings in the config:

| Setting | Current Value | Needed |
|---------|--------------|--------|
| `BR2_arm` | `y` (32-bit ARM) | `BR2_aarch64=y` |
| `BR2_cortex_a9` | `y` | Cortex-A53 or similar |
| `BR2_ARM_EABIHF` | `y` | LP64 ABI |
| `BR2_GCC_TARGET_ABI` | `aapcs-linux` | `lp64` |
| `BR2_GCC_TARGET_CPU` | `cortex-a9` | `cortex-a53` |
| `BR2_GCC_TARGET_FPU` | `neon` | (implicit in AArch64) |
| Sysroot triple | `arm-buildroot-linux-gnueabihf` | `aarch64-buildroot-linux-gnu` |

## Buildroot Config Details

**File:** `docker/support/rg35xx-buildroot-2017.11.config`

### Architecture

```
BR2_arm=y
BR2_cortex_a9=y
BR2_ARM_EABIHF=y
BR2_ARM_FPU_NEON=y
BR2_ARM_INSTRUCTIONS_THUMB2=y
```

### Toolchain Versions

| Component | Version |
|-----------|---------|
| Buildroot | 2017.11 |
| GCC | 7.2.0 (`BR2_GCC_VERSION_7_X`) |
| Binutils | 2.28.1 (`BR2_BINUTILS_VERSION_2_28_X`) |
| Kernel headers | 3.10.108 (`BR2_KERNEL_HEADERS_3_10`) |
| glibc | 2.28 (`BR2_TOOLCHAIN_BUILDROOT_GLIBC`) |

### Packages Enabled

- **FFmpeg** — all encoders, decoders, muxers, demuxers, parsers, protocols, filters; nonfree codecs enabled
- **SDL2** — with KMS/DRM support, SDL2_image (PNG), SDL2_mixer, SDL2_net, SDL2_TTF
- **SDL 1.2** (legacy) — with framebuffer backend, SDL_image, SDL_TTF
- **ALSA** — PCM, mixer, raw MIDI, HWDEP, sequencer plugins
- **Graphics** — FreeType, libjpeg (SIMD), libpng, libdrm, zlib
- **Utilities** — evtest, tree

### Other Settings

- Optimization: size (`BR2_OPTIMIZE_S`)
- LTO and OpenMP enabled
- No stack smashing protection (`BR2_SSP_NONE`)
- Shared libraries only (`BR2_SHARED_LIBS`)
- Extra libs: libasan (`BR2_TOOLCHAIN_EXTRA_LIBS="libasan"`)

## Patches Applied

All patches live in `docker/support/patches/`.

### glibc 2.28 Compatibility Fixes

Three packages fail to build against glibc 2.28 because `_IO_ftrylockfile` was removed from the private API:

- **bison** (`package/bison/110-glibc-change-work-around.patch`) — replaces `_IO_ftrylockfile` checks with `_IO_EOF_SEEN`, adds `_IO_IN_BACKUP` macro
- **m4** (`package/m4/000-fix-fseeko.patch`) — same pattern across fflush.c, fpending.c, fpurge.c, freadahead.c, freading.c, fseeko.c, stdio-impl.h
- **e2fsprogs** (`package/e2fsprogs/000-fix-gcc-conflict.patch`) — renames `copy_file_range()` to `copy_file_chunk()` to avoid conflict with the new glibc function of the same name

### SDL 1.2 RG35XX Patches

- **`package/sdl/0003-rg35xx-sdlk-additions.patch`** — adds 9 RG35XX-specific scancodes and key mappings (KATAKANA, HIRAGANA, HENKAN, POWER, etc.) to SDL 1.2's framebuffer input driver. **Not relevant for SDL2.**
- **`package/sdl/0004-modernize-SDL_FBCON_DONT_CLEAR.patch`** — changes framebuffer clearing behavior: skips palette restoration and pixel clearing when `dontClearPixels` is set or `SDL_DOUBLEBUF` is used. **Not relevant for SDL2.**

### Buildroot Framework Patch

- **`toolchain-expose-BR2_TOOLCHAIN_EXTRA_EXTERNAL_LIBS-for-all-toolchain-types-2017.11.1.diff`** — backports `BR2_TOOLCHAIN_EXTRA_LIBS` config option so it works with the internal Buildroot toolchain (not just external). Required for the `libasan` extra lib.

## Possible Approaches

> **Update (2026-02):** Now that we've inspected the device (see [Target Device](#target-device)), approach A is confirmed to work — a native arm64 Docker container build produces binaries that run on the device with no issues. The Buildroot toolchain is unnecessary since all required shared libraries are already present on the device. Approaches B and C are kept below for historical reference.

### A: Native arm64 Docker Container Build (Confirmed Working)

Use `apt` with `dpkg --add-architecture arm64` inside a Debian Docker container to install aarch64 cross-compiler and libraries directly from Debian repos.

**Pros:**
- Modern toolchain (GCC 12+, recent glibc, recent FFmpeg/SDL2)
- No Buildroot config to maintain, no patches needed
- Fast setup — apt install instead of multi-hour Buildroot build
- Libraries match what's on the device (Debian-based firmware)

**Cons:**
- Library versions must match the device's firmware (or ship them)
- Less control over exact build options

**Outline:**
```dockerfile
FROM debian:bookworm
RUN dpkg --add-architecture arm64 && apt-get update && \
    apt-get install -y gcc-aarch64-linux-gnu \
    libsdl2-dev:arm64 libsdl2-image-dev:arm64 \
    libavformat-dev:arm64 libavcodec-dev:arm64 \
    libavutil-dev:arm64 libswresample-dev:arm64
```

### B: Fix Buildroot Config for aarch64

Stay with Buildroot but update the defconfig to target aarch64 instead of ARM.

**Pros:**
- Minimal conceptual change — same pipeline, just different arch settings
- Full control over every library version

**Cons:**
- Buildroot 2017.11 is ancient and may not have good aarch64 support
- Would likely need to upgrade Buildroot to a modern release (2024.x)
- Upgrading Buildroot may break existing patches or require new ones
- Multi-hour rebuild from scratch
- Still maintaining a custom Buildroot config long-term

**Key config changes needed:**
```
BR2_aarch64=y
# Remove all BR2_arm, BR2_cortex_a9, BR2_ARM_* settings
# Update kernel headers to match device
```

### C: Skip Docker, Cross-Compile on Host Directly

Install `aarch64-linux-gnu-gcc` and aarch64 libraries on the host system (e.g. Arch Linux) without Docker.

**Pros:**
- Simplest setup — just `pacman -S aarch64-linux-gnu-gcc` and aarch64 libs
- No Docker overhead
- Fast iteration

**Cons:**
- Not reproducible across machines without documenting exact packages
- Harder to match device library versions
- Host distro package availability varies (Arch has good cross-compile support, others may not)

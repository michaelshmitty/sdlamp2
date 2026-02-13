# Cross-Compilation Toolchain

This document describes the target device and the arm64 Docker build container used to produce binaries for it.

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

## Build Container

The `docker-arm64/` directory contains a Docker setup that runs an arm64 Ubuntu 22.04 container via QEMU user-mode emulation. This matches the target device exactly — same distro, same glibc 2.35, same library versions. The project's native `make` (using `pkg-config`) works as-is inside the container, no cross-compilation flags needed.

### Prerequisites

QEMU binfmt handlers must be registered on the host (one-time setup, persists across reboots):

```sh
docker run --privileged --rm tonistiigi/binfmt --install arm64
```

### Usage

From the `docker-arm64/` directory:

```sh
make build              # one-shot: build sdlamp2 inside the container
make shell              # interactive bash shell for development
make clean              # remove the Docker image
```

The container mounts the project root at `/workspace`. Output binary is `build/sdlamp2` (`ELF 64-bit ARM aarch64`).

### Image Contents

- **Base:** `ubuntu:22.04` (arm64)
- **Build tools:** `build-essential`, `pkg-config`
- **SDL2:** `libsdl2-dev`, `libsdl2-image-dev`
- **FFmpeg:** `libavformat-dev`, `libavcodec-dev`, `libavutil-dev`, `libswresample-dev`

The image is tagged `arm64-dev` and is generic enough to reuse for other projects targeting the same device.

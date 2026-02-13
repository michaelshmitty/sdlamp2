# Build Container

arm64 Docker build container for producing device binaries. For target device details see [`rg35xx-plus.md`](rg35xx-plus.md).

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

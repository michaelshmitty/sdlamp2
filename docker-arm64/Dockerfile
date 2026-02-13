FROM ubuntu:22.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential pkg-config \
    libsdl2-dev libsdl2-image-dev \
    libavformat-dev libavcodec-dev libavutil-dev libswresample-dev \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /workspace

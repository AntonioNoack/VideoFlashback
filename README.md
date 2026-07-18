# VideoFlashback

VideoFlashback is a compact, high-performance gameplay replay recorder for Linux. It captures video and audio via PipeWire/SPA, buffers the last 30 seconds of activity in memory, and writes it to an MP4 file when you press `Super + G` (Windows Key + G).

## Features

- **PipeWire Integration**: Captured screens and audio streams are piped through PipeWire & SPA.
- **In-Memory Buffering**: Constantly maintains a sliding 30-second buffer in memory, preventing continuous disk writes.
- **Low-Level Hotkey Input**: Monitors hotkey events using `/dev/input/event*`.

## Dependencies

Install the required development libraries on Ubuntu/Debian:

```bash
sudo apt install \
    build-essential \
    cmake \
    pkg-config \
    libpipewire-0.3-dev \
    libspa-0.2-dev \
    libavcodec-dev \
    libavformat-dev \
    libavutil-dev \
    libswscale-dev \
    libswresample-dev \
    libavdevice-dev \
    libdbus-1-dev \
    libx11-dev
```

## Build

Build the project using CMake:

```bash
mkdir build
cd build
cmake ..
make
```

This generates the `replay` executable.

## Usage

1. **Configure keyboard device**: Identify the input event path corresponding to your keyboard under `/dev/input/event*`. If it differs from the default `/dev/input/event3` configured in `src/App.cpp`, update the device path there.
2. **Run the utility**:
   ```bash
   sudo ./replay
   ```
   *(Note: Reading `/dev/input/event*` typically requires root privileges or membership in the `input` group).*
3. **Capture replay**: Press `Super + G` (Windows Key + G) during gameplay to save the last 30 seconds to `replay.mp4` in your working directory.

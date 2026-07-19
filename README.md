# VideoFlashback

VideoFlashback is a compact gameplay replay recorder for Linux. It continuously captures screen and audio via PipeWire, keeps the last 30 seconds in memory, and writes an MP4 when you fire the trigger.

It works like Windows Game Bar (`Win + G`): a background server records into a ring buffer, and a separate trigger program saves a clip on demand.

## How it works

| Component | Role |
| --- | --- |
| `flashback-server` | Runs in the background. Captures video/audio, encodes H.264/AAC, and holds a 30-second in-memory buffer. Listens on `/tmp/videoflashback.sock`. |
| `flashback-trigger` | Connects to that socket and tells the server to save `replay.mp4`. Bind this binary to a global shortcut in your desktop environment. |

## Dependencies

Ubuntu/Debian:

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

```bash
mkdir build
cd build
cmake ..
make
```

This produces `flashback-server` and `flashback-trigger` in the build directory.

## Usage

1. **Start the server** (leave it running in the background):

   ```bash
   ./flashback-server
   ```

   On first start, approve the PipeWire/xdg-desktop-portal screen share prompt. The server prints when capture is active and when the control socket is ready.

2. **Register the trigger as a global shortcut** in your desktop settings (GNOME, KDE, Cosmic, etc.):

   - **Command:** absolute path to `flashback-trigger`, e.g. `/home/you/VideoFlashback/build/flashback-trigger`
   - **Shortcut:** whatever you prefer (e.g. `Super + G`)

3. **Capture a replay:** press your shortcut. The server writes `replay.mp4` in its current working directory (the directory from which you started `flashback-server`).

You can also run the trigger manually:

```bash
./flashback-trigger
```

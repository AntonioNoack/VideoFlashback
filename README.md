# VideoFlashback

VideoFlashback is a compact gameplay replay recorder for Linux. It continuously captures screen and audio via PipeWire, keeps a sliding buffer in memory, and writes an MP4 when you fire the trigger.

It works like Windows Game Bar (`Win + G`): a background server records into a ring buffer, and a separate trigger program saves a clip on demand.

## How it works

| Component | Role |
| --- | --- |
| `flashback-server` | Runs in the background. Captures video/audio, encodes, and holds the ring buffer. Listens on a Unix socket (default `/tmp/videoflashback.sock`). |
| `flashback-trigger` | Connects to that socket and tells the server to save a clip. Bind this binary to a global shortcut in your desktop environment. |

By default, clips are written to `~/Videos/Captures/` with names like `2024-10-29 17-08-18.mp4`.

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

CMake also fetches [toml++](https://github.com/marzer/tomlplusplus) automatically (needs network on the first configure).

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

3. **Capture a replay:** press your shortcut. The server saves a timestamped MP4 under `~/Videos/Captures/` (or your configured directory).

You can also run the trigger manually:

```bash
./flashback-trigger
```

## Configuration

Optional TOML config (missing file uses defaults):

`~/.config/videoflashback/config.toml`

or `$XDG_CONFIG_HOME/videoflashback/config.toml`

See `config.example.toml` for the full set. Summary:

```toml
[output]
directory = "~/Videos/Captures"
filename_format = "%Y-%m-%d %H-%M-%S.mp4"
notify_command = "notify-send 'VideoFlashback' 'Saved %n'"
socket_path = "/tmp/videoflashback.sock"

[replay]
buffer_seconds = 30

[video]
capture_fps = 60
scale = "native"              # or "1920x1080"
encoding = "h264"             # or "hevc"
rate_control = "bitrate"      # or "crf"
bitrate = 12000000
crf = 23
keyframe_interval_sec = 1.0
preset = "veryfast"
tune = "zerolatency"          # empty string to disable
hw_encoder = ""               # e.g. "h264_nvenc", "h264_vaapi"
pixel_format = "bgra"
max_queue_frames = 120
include_cursor = true

[audio]
sample_rate = 48000
channels = 2
bitrate = 128000
device = "default"            # or a PipeWire node id
```

Notes:

- `filename_format` uses [`strftime`](https://man7.org/linux/man-pages/man3/strftime.3.html).
- `notify_command` placeholders: `%f` full path, `%d` directory, `%n` filename.
- Encode resolution comes from the PipeWire capture when `scale = "native"`.
- `flashback-trigger` reads the same config for `socket_path`.

## Notes

- The server must already be running; the trigger only signals a save.
- No `/dev/input` access and no root are required for hotkeys — your desktop’s shortcut system runs the trigger.

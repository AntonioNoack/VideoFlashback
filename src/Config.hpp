#pragma once

#include <cstdint>
#include <string>

struct Config
{
    // Directory for saved clips. Supports leading "~".
    std::string output_directory = "~/Videos/Captures";

    // strftime pattern for the filename (including extension).
    // Default matches: 2024-10-29 17-08-18.mp4
    std::string filename_format = "%Y-%m-%d %H-%M-%S.mp4";

    // Target capture/encode frame rate. Extra frames from PipeWire are dropped.
    int capture_fps = 60;

    // Encode resolution. Empty / "native" = use the captured size.
    // Otherwise "WIDTHxHEIGHT", e.g. "1920x1080".
    std::string scale = "native";

    // Video codec name: "h264" or "hevc" (alias "h265").
    std::string encoding = "h264";

    // Target video bitrate in bits per second.
    int64_t bitrate = 12'000'000;

    // x264/x265 preset (veryfast, fast, medium, ...).
    std::string preset = "veryfast";

    // Audio capture/encode sample rate in Hz.
    int sample_rate = 48000;
};

// Load ~/.config/videoflashback/config.toml (or $XDG_CONFIG_HOME/...).
// Missing file → defaults. Invalid file → defaults + warning on stderr.
Config load_config();

// Expand "~" / "$HOME" at the start of a path.
std::string expand_user_path(const std::string& path);

// Build output path from config + current local time, creating the
// destination directory if needed. Returns empty string on failure.
std::string make_capture_path(const Config& config);

// Parse "WIDTHxHEIGHT". Returns false for "native", empty, or invalid.
bool parse_scale(const std::string& scale, int& width, int& height);

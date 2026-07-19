#pragma once

#include <cstdint>
#include <string>

struct Config
{
    // --- output ---
    std::string output_directory = "~/Videos/Captures";
    std::string filename_format = "%Y-%m-%d %H-%M-%S.mp4";
    // Shell command after a successful save. Empty = disabled.
    // Placeholders: %f = full path, %d = directory, %n = filename.
    std::string notify_command;
    std::string socket_path = "/tmp/videoflashback.sock";

    // --- replay ---
    int buffer_seconds = 30;

    // --- video ---
    int capture_fps = 60;
    std::string scale = "native";
    std::string encoding = "h264";
    // "bitrate" or "crf"
    std::string rate_control = "bitrate";
    int64_t bitrate = 12'000'000;
    int crf = 23;
    double keyframe_interval_sec = 1.0;
    std::string preset = "veryfast";
    // Empty disables tune. Default matches low-latency replay.
    std::string tune = "zerolatency";
    // Empty = software encoder from `encoding`. Else FFmpeg name, e.g. h264_nvenc.
    std::string hw_encoder;
    std::string pixel_format = "bgra";
    // Drop oldest pending frames when the encode queue exceeds this (0 = unlimited).
    int max_queue_frames = 120;
    bool include_cursor = true;

    // --- audio ---
    int sample_rate = 48000;
    int channels = 2;
    int64_t audio_bitrate = 128000;
    // "default" / "auto" = detect default sink; otherwise PipeWire node id.
    std::string audio_device = "default";
};

Config load_config();

std::string expand_user_path(const std::string& path);

std::string make_capture_path(const Config& config);

bool parse_scale(const std::string& scale, int& width, int& height);

// Run notify_command with path substitutions. No-op if empty.
void run_notify_command(const Config& config, const std::string& saved_path);

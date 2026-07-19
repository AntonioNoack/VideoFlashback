#pragma once

#include <string>

struct Config
{
    // Directory for saved clips. Supports leading "~".
    std::string output_directory = "~/Videos/Captures";

    // strftime pattern for the filename (including extension).
    // Default matches: 2024-10-29 17-08-18.mp4
    std::string filename_format = "%Y-%m-%d %H-%M-%S.mp4";
};

// Load ~/.config/videoflashback/config.toml (or $XDG_CONFIG_HOME/...).
// Missing file → defaults. Invalid file → defaults + warning on stderr.
Config load_config();

// Expand "~" / "$HOME" at the start of a path.
std::string expand_user_path(const std::string& path);

// Build output path from config + current local time, creating the
// destination directory if needed. Returns empty string on failure.
std::string make_capture_path(const Config& config);

#include "Config.hpp"

#include <toml++/toml.hpp>

#include <chrono>
#include <cctype>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

static std::string home_directory()
{
    if (const char* home = std::getenv("HOME"))
        return home;
    return {};
}

static std::string default_config_path()
{
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && xdg[0] != '\0')
        return std::string(xdg) + "/videoflashback/config.toml";

    const std::string home = home_directory();
    if (home.empty())
        return {};

    return home + "/.config/videoflashback/config.toml";
}

static std::string replace_all(
    std::string text,
    const std::string& from,
    const std::string& to)
{
    if (from.empty())
        return text;

    std::size_t pos = 0;
    while ((pos = text.find(from, pos)) != std::string::npos)
    {
        text.replace(pos, from.size(), to);
        pos += to.size();
    }
    return text;
}

std::string expand_user_path(const std::string& path)
{
    if (path.empty())
        return path;

    if (path == "~" || path.rfind("~/", 0) == 0)
    {
        const std::string home = home_directory();
        if (home.empty())
            return path;
        if (path.size() == 1)
            return home;
        return home + path.substr(1);
    }

    return path;
}

bool parse_scale(const std::string& scale, int& width, int& height)
{
    width = 0;
    height = 0;

    if (scale.empty())
        return false;

    std::string lower;
    lower.reserve(scale.size());
    for (char c : scale)
        lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));

    if (lower == "native" || lower == "source" || lower == "auto")
        return false;

    const auto x_pos = scale.find('x');
    if (x_pos == std::string::npos || x_pos == 0 || x_pos + 1 >= scale.size())
        return false;

    try
    {
        width = std::stoi(scale.substr(0, x_pos));
        height = std::stoi(scale.substr(x_pos + 1));
    }
    catch (...)
    {
        return false;
    }

    return width > 0 && height > 0;
}

static void load_string(const toml::table& table, const char* key, std::string& out)
{
    if (const auto* value = table[key].as_string())
        out = value->get();
}

static void load_int(const toml::table& table, const char* key, int& out)
{
    if (const auto* value = table[key].as_integer())
        out = static_cast<int>(value->get());
}

static void load_int64(const toml::table& table, const char* key, int64_t& out)
{
    if (const auto* value = table[key].as_integer())
        out = value->get();
}

static void load_double(const toml::table& table, const char* key, double& out)
{
    if (const auto* value = table[key].as_floating_point())
        out = value->get();
    else if (const auto* value = table[key].as_integer())
        out = static_cast<double>(value->get());
}

static void load_bool(const toml::table& table, const char* key, bool& out)
{
    if (const auto* value = table[key].as_boolean())
        out = value->get();
}

Config load_config()
{
    Config config;

    const std::string path = default_config_path();
    if (path.empty())
    {
        std::cerr << "HOME/XDG_CONFIG_HOME unset; using built-in defaults\n";
        return config;
    }

    if (!fs::exists(path))
    {
        std::cout << "No config at " << path << "; using defaults\n";
        return config;
    }

    try
    {
        const toml::table table = toml::parse_file(path);

        if (const auto* output = table["output"].as_table())
        {
            load_string(*output, "directory", config.output_directory);
            load_string(*output, "filename_format", config.filename_format);
            load_string(*output, "notify_command", config.notify_command);
            load_string(*output, "socket_path", config.socket_path);
        }

        if (const auto* replay = table["replay"].as_table())
        {
            load_int(*replay, "buffer_seconds", config.buffer_seconds);
        }

        if (const auto* video = table["video"].as_table())
        {
            load_int(*video, "capture_fps", config.capture_fps);
            load_string(*video, "scale", config.scale);
            load_string(*video, "encoding", config.encoding);
            load_string(*video, "rate_control", config.rate_control);
            load_int64(*video, "bitrate", config.bitrate);
            load_int(*video, "crf", config.crf);
            load_double(*video, "keyframe_interval_sec", config.keyframe_interval_sec);
            load_string(*video, "preset", config.preset);
            load_string(*video, "tune", config.tune);
            load_string(*video, "hw_encoder", config.hw_encoder);
            load_string(*video, "pixel_format", config.pixel_format);
            load_int(*video, "max_queue_frames", config.max_queue_frames);
            load_bool(*video, "include_cursor", config.include_cursor);
        }

        if (const auto* audio = table["audio"].as_table())
        {
            load_int(*audio, "sample_rate", config.sample_rate);
            load_int(*audio, "channels", config.channels);
            load_int64(*audio, "bitrate", config.audio_bitrate);
            load_string(*audio, "device", config.audio_device);
        }

        auto clamp_positive = [](int& value, int fallback, const char* name)
        {
            if (value <= 0)
            {
                std::cerr << "Invalid " << name << "; falling back to " << fallback << "\n";
                value = fallback;
            }
        };

        clamp_positive(config.capture_fps, 60, "capture_fps");
        clamp_positive(config.buffer_seconds, 30, "buffer_seconds");
        clamp_positive(config.sample_rate, 48000, "sample_rate");
        clamp_positive(config.channels, 2, "channels");
        clamp_positive(config.crf, 23, "crf");

        if (config.bitrate <= 0)
        {
            std::cerr << "Invalid bitrate; falling back to 12000000\n";
            config.bitrate = 12'000'000;
        }
        if (config.audio_bitrate <= 0)
        {
            std::cerr << "Invalid audio bitrate; falling back to 128000\n";
            config.audio_bitrate = 128000;
        }
        if (config.keyframe_interval_sec <= 0.0)
        {
            std::cerr << "Invalid keyframe_interval_sec; falling back to 1.0\n";
            config.keyframe_interval_sec = 1.0;
        }
        if (config.max_queue_frames < 0)
            config.max_queue_frames = 0;

        if (config.socket_path.empty())
            config.socket_path = "/tmp/videoflashback.sock";

        std::cout << "Loaded config from " << path << "\n";
    }
    catch (const toml::parse_error& err)
    {
        std::cerr << "Failed to parse " << path << ": " << err << "\n"
                  << "Using built-in defaults\n";
    }

    return config;
}

std::string make_capture_path(const Config& config)
{
    const std::string directory = expand_user_path(config.output_directory);
    if (directory.empty())
    {
        std::cerr << "Output directory is empty\n";
        return {};
    }

    std::error_code ec;
    fs::create_directories(directory, ec);
    if (ec)
    {
        std::cerr << "Could not create " << directory << ": " << ec.message() << "\n";
        return {};
    }

    const auto now = std::chrono::system_clock::now();
    const std::time_t now_t = std::chrono::system_clock::to_time_t(now);
    std::tm local_tm{};
    if (!localtime_r(&now_t, &local_tm))
    {
        std::cerr << "localtime_r failed\n";
        return {};
    }

    char filename[256];
    const std::size_t written = std::strftime(
        filename,
        sizeof(filename),
        config.filename_format.c_str(),
        &local_tm);

    if (written == 0)
    {
        std::cerr << "strftime failed for format: "
                  << config.filename_format << "\n";
        return {};
    }

    return (fs::path(directory) / filename).string();
}

void run_notify_command(const Config& config, const std::string& saved_path)
{
    if (config.notify_command.empty())
        return;

    const fs::path path(saved_path);
    std::string command = config.notify_command;
    command = replace_all(command, "%f", saved_path);
    command = replace_all(command, "%d", path.parent_path().string());
    command = replace_all(command, "%n", path.filename().string());

    const int status = std::system(command.c_str());
    if (status != 0)
    {
        std::cerr << "notify_command exited with status " << status
                  << ": " << command << "\n";
    }
}

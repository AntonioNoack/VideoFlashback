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
            if (const auto* dir = (*output)["directory"].as_string())
                config.output_directory = dir->get();

            if (const auto* fmt = (*output)["filename_format"].as_string())
                config.filename_format = fmt->get();
        }

        if (const auto* video = table["video"].as_table())
        {
            if (const auto* fps = (*video)["capture_fps"].as_integer())
                config.capture_fps = static_cast<int>(fps->get());

            if (const auto* scale = (*video)["scale"].as_string())
                config.scale = scale->get();

            if (const auto* encoding = (*video)["encoding"].as_string())
                config.encoding = encoding->get();

            if (const auto* bitrate = (*video)["bitrate"].as_integer())
                config.bitrate = bitrate->get();

            if (const auto* preset = (*video)["preset"].as_string())
                config.preset = preset->get();
        }

        if (const auto* audio = table["audio"].as_table())
        {
            if (const auto* rate = (*audio)["sample_rate"].as_integer())
                config.sample_rate = static_cast<int>(rate->get());
        }

        if (config.capture_fps <= 0)
        {
            std::cerr << "Invalid capture_fps; falling back to 60\n";
            config.capture_fps = 60;
        }

        if (config.bitrate <= 0)
        {
            std::cerr << "Invalid bitrate; falling back to 12000000\n";
            config.bitrate = 12'000'000;
        }

        if (config.sample_rate <= 0)
        {
            std::cerr << "Invalid sample_rate; falling back to 48000\n";
            config.sample_rate = 48000;
        }

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

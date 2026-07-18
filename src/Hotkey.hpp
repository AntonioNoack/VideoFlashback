#pragma once

#include <string>

// sudo evtest


class Hotkey
{
public:

    bool initialize(const std::string& device);
    bool pressed();

private:

    int fd = -1;

    bool super_down = false;
    bool g_down = false;
};
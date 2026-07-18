#pragma once

#include <X11/Xlib.h>


class Hotkey
{
public:

    bool initialize();
    bool pressed();

    ~Hotkey();

private:

    Display* display = nullptr;
    Window root;
    KeyCode key;

};
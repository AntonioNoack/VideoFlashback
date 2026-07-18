#include "Hotkey.hpp"

#include <X11/keysym.h>


bool Hotkey::initialize()
{
    display =
        XOpenDisplay(nullptr);


    if (!display)
        return false;


    root = DefaultRootWindow(display);

    key = XKeysymToKeycode(
            display,
            XK_g);

    XGrabKey(
        display,
        key,
        Mod4Mask,
        root,
        True,
        GrabModeAsync,
        GrabModeAsync);


    XSelectInput(
        display,
        root,
        KeyPressMask);


    XFlush(display);


    return true;
}



bool Hotkey::pressed()
{
    while (XPending(display))
    {
        XEvent event;

        XNextEvent(
            display,
            &event);


        if(event.type == KeyPress)
        {
            if(event.xkey.keycode == key)
                return true;
        }
    }


    return false;
}



Hotkey::~Hotkey()
{
    if(display)
    {
        XUngrabKey(
            display,
            key,
            Mod4Mask,
            root);


        XCloseDisplay(
            display);
    }
}
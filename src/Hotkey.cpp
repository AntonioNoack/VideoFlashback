#include "Hotkey.hpp"

#include <linux/input.h>

#include <fcntl.h>
#include <unistd.h>

#include <iostream>


bool Hotkey::initialize(
    const std::string& device)
{
    fd =
        open(
            device.c_str(),
            O_RDONLY | O_NONBLOCK);


    if(fd < 0)
    {
        perror("open input device");
        return false;
    }


    return true;
}



bool Hotkey::pressed()
{
    input_event event;


    while(read(
        fd,
        &event,
        sizeof(event)) == sizeof(event))
    {
        if(event.type != EV_KEY)
            continue;


        switch(event.code)
        {
            case KEY_LEFTMETA:
            case KEY_RIGHTMETA:

                super_down =
                    event.value != 0;

                break;


            case KEY_G:

                g_down =
                    event.value != 0;

                break;
        }


        if(super_down && g_down)
        {
            g_down = false;
            return true;
        }
    }


    return false;
}
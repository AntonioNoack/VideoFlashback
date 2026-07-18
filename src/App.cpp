#include "App.hpp"
#include "Capture.hpp"

#include <iostream>

int App::run()
{
    std::cout << "Replay recorder\n";
    Capture capture;

    if (!capture.initialize())
    {
        return 1;
    }

    bool running = true;
    while (running)
    {
        capture.update();
    }

    return 0;
}
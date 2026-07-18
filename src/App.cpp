#include "App.hpp"
#include "Capture.hpp"
#include "ScreenCast.hpp"

#include <iostream>

int App::run()
{
    std::cout << "Replay recorder\n";
    Capture capture;

    if (!capture.initialize()) {
        return 1;
    }

    ScreenCast screen;
    if (!screen.initialize()) {
        return 1;
    }

    uint32_t node = screen.start();
    if (!node) {
        return 1;
    }

    capture.connect_to_node(node);

    bool running = true;
    while (running) {
        capture.update();
    }

    return 0;
}
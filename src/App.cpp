#include "App.hpp"
#include "Capture.hpp"
#include "ScreenCast.hpp"

#include <iostream>

int App::run()
{
    std::cout << "Replay recorder\n";

    ScreenCast screen;
    if (!screen.initialize()) {
        return 1;
    }

    uint32_t node = screen.start();
    if (!node) {
        return 1;
    }

    Capture capture;
    if (!capture.initialize()) {
        return 1;
    }
    capture.set_frame_callback(
        [](const uint8_t* data,
        uint32_t width,
        uint32_t height,
        uint32_t stride,
        uint64_t timestamp)
        {
            static uint64_t frames = 0;

            frames++;

            if (frames % 10 == 0)
            {
                std::cout
                    << "Received "
                    << frames
                    << " frames\n";
            }
        });
    capture.connect_to_node(node);

    bool running = true;
    while (running) {
        capture.update();
    }

    return 0;
}
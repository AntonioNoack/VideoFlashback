#include "App.hpp"
#include "Capture.hpp"
#include "Encoder.hpp"
#include "RingBuffer.hpp"
#include "ScreenCast.hpp"

#include <iostream>

int App::run()
{
    std::cout << "Replay recorder\n";

    RingBuffer buffer(30);
    Encoder encoder(buffer);

    ScreenCast screen;
    if (!screen.initialize()) {
        return 1;
    }

    uint32_t node = screen.start();
    if (!node) {
        return 1;
    }

    encoder.initialize(3840, 2160);

    Capture capture;
    if (!capture.initialize()) {
        return 1;
    }

    capture.set_frame_callback(
        [&encoder](const uint8_t* data,
        uint32_t width,
        uint32_t height,
        uint32_t stride,
        uint64_t timestamp)
        {
            RawFrame frame;

            frame.width = width;
            frame.height = height;
            frame.stride = stride;

            frame.data.assign(
                data,
                data + stride * height);

            encoder.push(std::move(frame));
        }
    );
    capture.connect_to_node(node);


    bool running = true;
    while (running) {
        capture.update();
    }

    return 0;
}
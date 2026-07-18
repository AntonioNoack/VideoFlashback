#include "App.hpp"
#include "Capture.hpp"
#include "Encoder.hpp"
#include "Hotkey.hpp"
#include "ReplayWriter.hpp"
#include "RingBuffer.hpp"
#include "ScreenCast.hpp"

#include <iostream>

#include <thread>
#include <chrono>

int App::run()
{
    std::cout << "Replay recorder\n";

    RingBuffer videoBuffer(30);
    Encoder encoder(videoBuffer);
    ReplayWriter writer;

    Hotkey hotkey;
    if(!hotkey.initialize()) {
        std::cerr << "Hotkey failed\n";
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
            frame.timestamp_ns = timestamp;

            frame.data.assign(
                data,
                data + stride * height);

            encoder.push(std::move(frame));
        }
    );
    capture.connect_to_node(node);

    volatile bool save_requested = false;
    /*std::thread capture_thread(
        [&capture, &writer, &hotkey, &save_requested]() {*/
            bool running = true;
            while (running) {
                capture.update();
                
                if (hotkey.pressed()) {
                    save_requested = true;
                    std::cout << "Hotkey pressed, writing file" << std::endl;
                    writer.write("replay.mp4", videoBuffer/*, audioBuffer*/);
                }
            }
    // });

    /*std::thread writer_thread(
        [&writer, &save_requested, &videoBuffer, &audioBuffer]() {
            bool running = true;
            while (running)
            {
                if (save_requested) {
                    save_requested = false;
                    writer.write("replay.mp4", videoBuffer, audioBuffer);
                }
            }
            
        }
    )*/

    /*std::this_thread::sleep_for(std::chrono::seconds(10));

    writer.write("replay.mp4", videoBuffer);

    capture_thread.detach();*/

    return 0;
}
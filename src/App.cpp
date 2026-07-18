#include "App.hpp"
#include "AudioCapture.hpp"
#include "AudioEncoder.hpp"
#include "VideoCapture.hpp"
#include "VideoEncoder.hpp"
#include "Hotkey.hpp"
#include "ReplayWriter.hpp"
#include "PacketBuffer.hpp"
#include "ScreenCast.hpp"

#include <iostream>

#include <thread>
#include <chrono>

int App::run()
{
    std::cout << "Replay recorder\n";

    int64_t numSeconds = 30;
    PacketBuffer buffer(numSeconds);
    AudioEncoder audioEncoder(buffer);
    VideoEncoder videoEncoder(buffer);

    ReplayWriter writer;

    Hotkey hotkey;
    if(!hotkey.initialize("/dev/input/event3")) {
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

    videoEncoder.initialize(3840, 2160);

    VideoCapture videoCapture;
    if (!videoCapture.initialize()) {
        return 1;
    }

    videoCapture.set_callback(
        [&videoEncoder](const uint8_t* data,
        uint32_t width,
        uint32_t height,
        uint32_t stride,
        uint64_t timestamp)
        {
            RawVideoFrame frame;

            frame.width = width;
            frame.height = height;
            frame.stride = stride;
            frame.timestamp_ns = timestamp;

            frame.data.assign(
                data,
                data + stride * height);

            videoEncoder.push(std::move(frame));
        });
    videoCapture.connect_to_node(node);

    audioEncoder.initialize(48000, 2);

    AudioCapture audioCapture;
    if (!audioCapture.initialize()) {
        return 1;
    }

    audioCapture.set_callback(
        [&audioEncoder](const float* data,
            uint32_t frames,
            uint32_t channels,
            uint64_t timestamp) {

            RawAudioFrame frame;

            // frame.frames = frames;
            frame.channels = channels;
            frame.sampleRate = 48000; // todo this should not be here
            frame.timestamp_ns = timestamp;

            frame.samples.assign(
                data,
                data + frames * channels);

            audioEncoder.push(std::move(frame));
        });
    audioCapture.connect_to_node(node);

    volatile bool save_requested = false;
    /*std::thread capture_thread(
        [&videoCapture, &writer, &hotkey, &save_requested]() {*/
            bool running = true;
            while (running) {
                audioCapture.update();
                videoCapture.update();
                
                if (hotkey.pressed()) {
                    save_requested = true;
                    std::cout << "Hotkey pressed, writing file" << std::endl;
                    writer.write("replay.mp4", buffer/*, audioBuffer*/);
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
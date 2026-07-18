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
#include <fcntl.h>
#include <unistd.h>
#include <cstdio>
#include <cctype>

static uint32_t get_default_sink_id() {
    FILE* fp = popen("wpctl status", "r");
    if (!fp) return 0;
    char line[512];
    bool in_sinks = false;
    uint32_t sink_id = 0;
    while (fgets(line, sizeof(line), fp)) {
        std::string s(line);
        if (s.find("Sinks:") != std::string::npos) {
            in_sinks = true;
            continue;
        }
        if (in_sinks) {
            if (s.find("Sink endpoints:") != std::string::npos ||
                s.find("Sources:") != std::string::npos ||
                s.find("Devices:") != std::string::npos) {
                break;
            }
            if (s.find('*') != std::string::npos) {
                size_t idx = s.find('*');
                while (idx < s.size() && !std::isdigit(static_cast<unsigned char>(s[idx]))) {
                    idx++;
                }
                if (idx < s.size()) {
                    try {
                        sink_id = std::stoul(s.substr(idx));
                    } catch (...) {
                    }
                    break;
                }
            }
        }
    }
    pclose(fp);
    return sink_id;
}

int App::run()
{
    std::cout << "Replay recorder\n";

    int64_t numSeconds = 30;
    PacketBuffer buffer(numSeconds);
    AudioEncoder audioEncoder(buffer);
    VideoEncoder videoEncoder(buffer);

    ReplayWriter writer;

    bool use_stdin_fallback = false;
    Hotkey hotkey;
    if(!hotkey.initialize("/dev/input/event3")) {
        std::cerr << "Hotkey initialization failed. Pressing Enter in the console will be used to trigger saves instead.\n";
        use_stdin_fallback = true;
        int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
        fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
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
    uint32_t audio_node = PW_ID_ANY;
    uint32_t default_sink = get_default_sink_id();
    if (default_sink != 0) {
        audio_node = default_sink;
        std::cout << "Targeting default audio sink monitor (node ID: " << default_sink << ")\n";
    } else {
        std::cout << "Could not detect default sink ID, falling back to default capture source.\n";
    }
    audioCapture.connect_to_node(audio_node);

    volatile bool save_requested = false;
    /*std::thread capture_thread(
        [&videoCapture, &writer, &hotkey, &save_requested]() {*/
            auto start_time = std::chrono::steady_clock::now();
            bool running = true;
            std::cout << "Capture started. Capturing for 10 seconds, then saving automatically to replay.mp4 and exiting...\n";
            while (running) {
                audioCapture.update();
                videoCapture.update();
                
                bool triggered = false;
                if (use_stdin_fallback) {
                    char dummy;
                    if (read(STDIN_FILENO, &dummy, 1) > 0) {
                        triggered = true;
                    }
                } else {
                    triggered = hotkey.pressed();
                }

                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::steady_clock::now() - start_time).count();
                if (elapsed >= 10) {
                    triggered = true;
                    running = false;
                    std::cout << "10 seconds elapsed. Saving automatically...\n";
                }

                if (triggered) {
                    save_requested = true;
                    std::cout << "Triggered! Writing replay.mp4..." << std::endl;
                    writer.write("replay.mp4", buffer);
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(1));
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
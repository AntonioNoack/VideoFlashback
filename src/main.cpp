#include "AudioCapture.hpp"
#include "AudioEncoder.hpp"
#include "Config.hpp"
#include "VideoCapture.hpp"
#include "VideoEncoder.hpp"
#include "ReplayWriter.hpp"
#include "PacketBuffer.hpp"
#include "ScreenCast.hpp"

#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <fcntl.h>
#include <unistd.h>
#include <cstdio>
#include <cctype>
#include <sys/socket.h>
#include <sys/un.h>
#include <cstring>

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

int main()
{
    std::cout << "Replay recorder service started\n";

    const Config config = load_config();
    std::cout << "Saving clips to "
              << expand_user_path(config.output_directory)
              << " (" << config.filename_format << ")\n";
    std::cout << "Video settings: "
              << config.capture_fps << " fps, scale=" << config.scale
              << ", encoding=" << config.encoding
              << ", bitrate=" << (config.bitrate / 1000) << " kbps"
              << ", preset=" << config.preset << "\n";
    std::cout << "Audio settings: " << config.sample_rate << " Hz\n";

    int64_t numSeconds = 30;
    PacketBuffer buffer(numSeconds);
    AudioEncoder audioEncoder(buffer);
    VideoEncoder videoEncoder(buffer);

    ReplayWriter writer;

    // Set up Unix Domain Socket for triggering replay saves
    std::string socket_path = "/tmp/videoflashback.sock";
    int server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd >= 0) {
        int flags = fcntl(server_fd, F_GETFL, 0);
        fcntl(server_fd, F_SETFL, flags | O_NONBLOCK);

        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        std::strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);

        unlink(socket_path.c_str());
        if (bind(server_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            perror("bind socket");
            close(server_fd);
            server_fd = -1;
        } else if (listen(server_fd, 5) < 0) {
            perror("listen socket");
            close(server_fd);
            server_fd = -1;
        } else {
            std::cout << "Control socket listening at " << socket_path << "\n";
        }
    } else {
        std::cerr << "Failed to create control socket\n";
    }

    ScreenCast screen;
    if (!screen.initialize()) {
        if (server_fd >= 0) { close(server_fd); unlink(socket_path.c_str()); }
        return 1;
    }

    uint32_t node = screen.start();
    if (!node) {
        if (server_fd >= 0) { close(server_fd); unlink(socket_path.c_str()); }
        return 1;
    }

    if (!videoEncoder.configure(config)) {
        if (server_fd >= 0) { close(server_fd); unlink(socket_path.c_str()); }
        return 1;
    }

    VideoCapture videoCapture;
    if (!videoCapture.initialize()) {
        if (server_fd >= 0) { close(server_fd); unlink(socket_path.c_str()); }
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

    audioEncoder.initialize(config.sample_rate, 2);

    AudioCapture audioCapture;
    if (!audioCapture.initialize()) {
        if (server_fd >= 0) { close(server_fd); unlink(socket_path.c_str()); }
        return 1;
    }

    audioCapture.set_preferred_format(
        static_cast<uint32_t>(config.sample_rate),
        2);

    audioCapture.set_callback(
        [&audioEncoder, &config](const float* data,
            uint32_t frames,
            uint32_t channels,
            uint64_t timestamp) {

            RawAudioFrame frame;

            frame.channels = channels;
            frame.sampleRate = config.sample_rate;
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

    bool running = true;
    std::cout << "Service capture started. Use flashback-trigger to save the last 30s.\n";
    while (running) {
        audioCapture.update();
        videoCapture.update();
        
        bool triggered = false;
        if (server_fd >= 0) {
            int client_fd = accept(server_fd, nullptr, nullptr);
            if (client_fd >= 0) {
                char buf[16];
                ssize_t bytes_read = read(client_fd, buf, sizeof(buf));
                if (bytes_read > 0) {
                    triggered = true;
                }
                close(client_fd);
            }
        }

        if (triggered) {
            const std::string path = make_capture_path(config);
            if (path.empty()) {
                std::cerr << "Could not build capture path; skipping save\n";
            } else {
                std::cout << "Trigger command received! Writing " << path << "...\n";
                writer.write(path, buffer);
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    if (server_fd >= 0) {
        close(server_fd);
        unlink(socket_path.c_str());
    }

    return 0;
}
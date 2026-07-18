#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include <spa/param/video/raw.h>
#include <spa/utils/dict.h>
#include <pipewire/pipewire.h>

struct pw_main_loop;
struct pw_context;
struct pw_core;
struct pw_registry;
struct spa_hook;

struct Frame
{
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t stride = 0;

    uint64_t timestamp = 0;

    std::vector<uint8_t> data;
};

using FrameCallback =
    std::function<void(
        const uint8_t* data,
        uint32_t width,
        uint32_t height,
        uint32_t stride,
        uint64_t timestamp)>;

class Capture
{
public:
    ~Capture();

    bool initialize();
    void update();
    void shutdown();

    bool connect_to_node(uint32_t node_id);

    void set_frame_callback(FrameCallback callback);

    static void on_stream_state_changed(
        void* data,
        enum pw_stream_state old_state,
        enum pw_stream_state state,
        const char* error);

    static void on_stream_param_changed(
        void* data,
        uint32_t id,
        const struct spa_pod* param);

    static void on_stream_process(
        void* data);

private:
    static void on_registry_global(
        void* data,
        uint32_t id,
        uint32_t permissions,
        const char* type,
        uint32_t version,
        const struct spa_dict* props);

private:
    bool initialized = false;

    pw_main_loop* loop = nullptr;
    pw_context* context = nullptr;
    pw_core* core = nullptr;
    pw_registry* registry = nullptr;

    pw_stream* stream = nullptr;

    spa_hook registry_listener;

private:

    uint32_t video_width = 0;
    uint32_t video_height = 0;
    uint32_t video_stride = 0;

    FrameCallback frame_callback;

struct spa_video_info_raw video_format{};

};
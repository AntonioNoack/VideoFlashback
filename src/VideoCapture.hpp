#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <spa/param/video/raw.h>
#include <spa/utils/dict.h>
#include <pipewire/pipewire.h>

struct pw_main_loop;
struct pw_context;
struct pw_core;
struct pw_registry;
struct spa_hook;

struct VideoFrame
{
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t stride = 0;

    uint64_t timestamp = 0;

    std::vector<uint8_t> data;
};

using VideoFrameCallback =
    std::function<void(
        const uint8_t* data,
        uint32_t width,
        uint32_t height,
        uint32_t stride,
        uint64_t timestamp)>;

class VideoCapture
{
public:
    ~VideoCapture();

    bool initialize();
    void update();
    void shutdown();

    bool connect_to_node(uint32_t node_id);

    // Preferred capture format name: bgra, rgba, bgr0, rgb0, argb, abgr.
    void set_preferred_pixel_format(const std::string& name);

    void set_callback(VideoFrameCallback callback);

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

    spa_video_format preferred_format = SPA_VIDEO_FORMAT_BGRA;

    VideoFrameCallback frame_callback;

struct spa_video_info_raw video_format{};

};
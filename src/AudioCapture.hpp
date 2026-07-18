
#pragma once

#include <cstdint>
#include <vector>
#include <functional>

#include <spa/param/audio/raw.h>
#include <spa/utils/dict.h>
#include <pipewire/pipewire.h>

struct pw_main_loop;
struct pw_context;
struct pw_core;
struct pw_registry;
struct spa_hook;

struct AudioFrame
{
    std::vector<float> samples;

    uint32_t frames;
    uint32_t channels;
    uint32_t sample_rate;
    uint64_t timestamp_ns;
};

using AudioFrameCallback =
    std::function<void(
        const float* samples,
        uint32_t frames,
        uint32_t channels,
        uint64_t timestamp)>;

class AudioCapture
{
public:
    ~AudioCapture();

    bool initialize();
    bool connect_to_node(uint32_t node_id);
    void update();
    void shutdown();

    void set_callback(AudioFrameCallback callback);

    static void on_stream_state_changed(
        void* data,
        enum pw_stream_state old_state,
        enum pw_stream_state state,
        const char* error);

    static void on_stream_param_changed(
        void* data,
        uint32_t id,
        const struct spa_pod* param);

    static void on_stream_process(void* data);

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

    uint32_t sample_rate = 0;
    uint32_t channels = 0;

    AudioFrameCallback frame_callback;
    struct spa_audio_info_raw audio_format{};
};
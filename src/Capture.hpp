#pragma once

#include <stdint.h>
#include <spa/utils/dict.h>
#include <pipewire/pipewire.h>

struct pw_main_loop;
struct pw_context;
struct pw_core;
struct pw_registry;
struct spa_hook;

class Capture
{
public:
    ~Capture();

    bool initialize();
    void update();
    void shutdown();

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
};
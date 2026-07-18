#include "Capture.hpp"

#include <iostream>

static const pw_stream_events stream_events =
{
    PW_VERSION_STREAM_EVENTS,

    .state_changed =
        Capture::on_stream_state_changed,

    .param_changed =
        Capture::on_stream_param_changed,

    .process =
        Capture::on_stream_process
};

bool Capture::initialize()
{
    if (initialized)
        return true;


    pw_init(nullptr, nullptr);


    loop = pw_main_loop_new(nullptr);

    if (!loop)
    {
        std::cerr << "Failed creating PipeWire loop\n";
        return false;
    }


    context = pw_context_new(
        pw_main_loop_get_loop(loop),
        nullptr,
        0);

    if (!context)
    {
        std::cerr << "Failed creating PipeWire context\n";
        return false;
    }


    core = pw_context_connect(
        context,
        nullptr,
        0);

    if (!core)
    {
        std::cerr << "Failed connecting PipeWire\n";
        return false;
    }

    stream = pw_stream_new_simple(
        pw_main_loop_get_loop(loop),
        "Replay Recorder",
        pw_properties_new(
            PW_KEY_MEDIA_TYPE,
            "Video",
            PW_KEY_MEDIA_CATEGORY,
            "Capture",
            PW_KEY_MEDIA_ROLE,
            "Screen",
            nullptr),
        &stream_events,
        this);


    // not needed anymore:
    /*registry = pw_core_get_registry(
        core,
        PW_VERSION_REGISTRY,
        0);


    static const pw_registry_events registry_events =
    {
        PW_VERSION_REGISTRY_EVENTS,
        .global = on_registry_global
    };


    pw_registry_add_listener(
        registry,
        &registry_listener,
        &registry_events,
        this);*/


    initialized = true;

    std::cout << "Connected to PipeWire\n";

    return true;
}


void Capture::update()
{
    if (!initialized)
        return;

    pw_loop_iterate(
        pw_main_loop_get_loop(loop),
        0);
}


void Capture::shutdown()
{
    if (!initialized)
        return;


    if (registry)
    {
        pw_proxy_destroy(
            reinterpret_cast<pw_proxy*>(registry));

        registry = nullptr;
    }


    if (core)
    {
        pw_core_disconnect(core);
        core = nullptr;
    }


    if (context)
    {
        pw_context_destroy(context);
        context = nullptr;
    }


    if (loop)
    {
        pw_main_loop_destroy(loop);
        loop = nullptr;
    }


    initialized = false;
}


Capture::~Capture()
{
    shutdown();
}



void Capture::on_registry_global(
    void* data,
    uint32_t id,
    uint32_t permissions,
    const char* type,
    uint32_t version,
    const struct spa_dict* props)
{
    auto* self =
        static_cast<Capture*>(data);


    if (!props)
        return;


    const char* name =
        spa_dict_lookup(
            props,
            "node.name");


    const char* description =
        spa_dict_lookup(
            props,
            "node.description");


    if (name || description)
    {
        std::cout
            << "Node "
            << id
            << ": ";

        if (description)
            std::cout << description;

        else if (name)
            std::cout << name;

        std::cout << "\n";
    }
}

void Capture::on_stream_state_changed(
    void* data,
    enum pw_stream_state old_state,
    enum pw_stream_state state,
    const char* error)
{
    std::cout
        << "Stream state: "
        << pw_stream_state_as_string(state)
        << "\n";

    if (error)
        std::cout << error << "\n";
}

void Capture::on_stream_param_changed(
    void* data,
    uint32_t id,
    const struct spa_pod* param)
{
    std::cout << "Stream format changed\n";
}

void Capture::on_stream_process(void* data)
{
    auto* self =
        static_cast<Capture*>(data);

    pw_buffer* buffer =
        pw_stream_dequeue_buffer(self->stream);

    if (!buffer)
        return;


    std::cout << "Frame received\n";


    pw_stream_queue_buffer(
        self->stream,
        buffer);
}
#include <chrono>
#include <iostream>

#include <spa/param/video/format-utils.h>
#include <pipewire/pipewire.h>

#include "Capture.hpp"

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
    auto* self =
        static_cast<Capture*>(data);

    if (!param) return;


    uint32_t width = 0;
    uint32_t height = 0;

    if (spa_format_video_raw_parse(
            param,
            &self->video_format) < 0)
    {
        std::cout
            << "Could not parse video format\n";

        return;
    }


    width = self->video_format.size.width;
    height = self->video_format.size.height;

    self->video_width = width;
    self->video_height = height;

    std::cout
        << "Video format "
        << width
        << "x"
        << height
        << "\n";
}


static int64_t now_nanoseconds()
{
    return std::chrono::duration_cast<
        std::chrono::nanoseconds>(
            std::chrono::steady_clock::now()
                .time_since_epoch())
        .count();
}

void Capture::on_stream_process(void* data)
{
    auto* self =
        static_cast<Capture*>(data);

    pw_buffer* buffer =
        pw_stream_dequeue_buffer(self->stream);

    if (!buffer)
        return;


    spa_buffer* spa_buffer = buffer->buffer;


    if (!spa_buffer ||
        spa_buffer->n_datas == 0)
    {
        pw_stream_queue_buffer(
            self->stream,
            buffer);

        return;
    }


    spa_data* spa_data_ptr =
        &spa_buffer->datas[0];


    if (!spa_data_ptr->data)
    {
        pw_stream_queue_buffer(
            self->stream,
            buffer);

        return;
    }

    if (self->frame_callback &&
        spa_data_ptr->data &&
        spa_data_ptr->chunk)
    {

        int64_t timestamp = 0;

        spa_meta_header* header =
            static_cast<spa_meta_header*>(
                spa_buffer_find_meta_data(
                    buffer->buffer,
                    SPA_META_Header,
                    sizeof(spa_meta_header)));


        struct pw_time time;
        if (header) {
            timestamp = header->pts;

            /*std::cout
                << "PipeWire header timestamp "
                << timestamp
                << "\n";*/
                
        } else if (false && pw_stream_get_time_n(
            self->stream,
            &time, sizeof(time)) == 0) // todo bug: this produces 0
        {
            timestamp = time.now;

            std::cout
                << "PipeWire clock timestamp "
                << timestamp
                << "\n";
        } else {
            timestamp = now_nanoseconds();

            /*std::cout
                << "Chrono timestamp "
                << timestamp
                << "\n";*/
        }

        self->frame_callback(
            static_cast<uint8_t*>(spa_data_ptr->data),

            self->video_width,
            self->video_height,

            spa_data_ptr->chunk->stride,

            timestamp);
    }


    if (false && spa_data_ptr->data) {
        std::cout
            << "Frame "
            << self->video_width
            << "x"
            << self->video_height
            << " bytes="
            << spa_data_ptr->chunk->size
            << "\n";
    }


    pw_stream_queue_buffer(
        self->stream,
        buffer);
}

bool Capture::connect_to_node(
    uint32_t node_id)
{
    if (!stream)
        return false;


    int result =
        pw_stream_connect(
            stream,
            PW_DIRECTION_INPUT,
            node_id,
            static_cast<pw_stream_flags>(
                PW_STREAM_FLAG_AUTOCONNECT |
                PW_STREAM_FLAG_MAP_BUFFERS),
            nullptr,
            0);


    if (result < 0)
    {
        std::cerr
            << "Failed connecting stream: "
            << result
            << "\n";

        return false;
    }


    std::cout
        << "PipeWire stream connected\n";


    return true;
}

void Capture::set_frame_callback(
    FrameCallback callback)
{
    frame_callback = std::move(callback);
}
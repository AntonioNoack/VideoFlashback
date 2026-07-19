#include <chrono>
#include <cctype>
#include <iostream>
#include <string>

#include <spa/param/video/format-utils.h>
#include <spa/pod/builder.h>
#include <pipewire/pipewire.h>

#include "VideoCapture.hpp"

static const pw_stream_events stream_events =
{
    PW_VERSION_STREAM_EVENTS,

    .state_changed =
        VideoCapture::on_stream_state_changed,

    .param_changed =
        VideoCapture::on_stream_param_changed,

    .process =
        VideoCapture::on_stream_process
};

bool VideoCapture::initialize()
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

    std::cout << "Video connected to PipeWire\n";

    return true;
}


void VideoCapture::update()
{
    if (!initialized)
        return;

    pw_loop_iterate(
        pw_main_loop_get_loop(loop),
        0);
}


void VideoCapture::shutdown()
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


VideoCapture::~VideoCapture()
{
    shutdown();
}



void VideoCapture::on_registry_global(
    void* data,
    uint32_t id,
    uint32_t permissions,
    const char* type,
    uint32_t version,
    const struct spa_dict* props)
{
    auto* self =
        static_cast<VideoCapture*>(data);


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

void VideoCapture::on_stream_state_changed(
    void* data,
    enum pw_stream_state old_state,
    enum pw_stream_state state,
    const char* error)
{
    std::cout
        << "Video stream state: "
        << pw_stream_state_as_string(state)
        << "\n";

    if (error)
        std::cout << error << "\n";
}

void VideoCapture::on_stream_param_changed(
    void* data,
    uint32_t id,
    const struct spa_pod* param)
{
    auto* self =
        static_cast<VideoCapture*>(data);

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

void VideoCapture::on_stream_process(void* data)
{
    auto* self =
        static_cast<VideoCapture*>(data);

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
            << "VideoFrame "
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

bool VideoCapture::connect_to_node(
    uint32_t node_id)
{
    if (!stream)
        return false;

    uint8_t buffer[1024];
    spa_pod_builder builder =
        SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));

    const spa_pod* params[1] = {
        static_cast<spa_pod*>(spa_pod_builder_add_object(
            &builder,
            SPA_TYPE_OBJECT_Format,
            SPA_PARAM_EnumFormat,
            SPA_FORMAT_mediaType,
            SPA_POD_Id(SPA_MEDIA_TYPE_video),
            SPA_FORMAT_mediaSubtype,
            SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw),
            SPA_FORMAT_VIDEO_format,
            SPA_POD_Id(preferred_format)))
    };

    int result =
        pw_stream_connect(
            stream,
            PW_DIRECTION_INPUT,
            node_id,
            static_cast<pw_stream_flags>(
                PW_STREAM_FLAG_AUTOCONNECT |
                PW_STREAM_FLAG_MAP_BUFFERS),
            params,
            1);


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

void VideoCapture::set_preferred_pixel_format(
    const std::string& name)
{
    std::string lower;
    lower.reserve(name.size());
    for (char c : name)
        lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));

    if (lower == "bgra")
        preferred_format = SPA_VIDEO_FORMAT_BGRA;
    else if (lower == "rgba")
        preferred_format = SPA_VIDEO_FORMAT_RGBA;
    else if (lower == "bgr0" || lower == "bgrx")
        preferred_format = SPA_VIDEO_FORMAT_BGRx;
    else if (lower == "rgb0" || lower == "rgbx")
        preferred_format = SPA_VIDEO_FORMAT_RGBx;
    else if (lower == "argb")
        preferred_format = SPA_VIDEO_FORMAT_ARGB;
    else if (lower == "abgr")
        preferred_format = SPA_VIDEO_FORMAT_ABGR;
    else
    {
        std::cerr << "Unknown pixel_format '" << name
                  << "'; keeping BGRA\n";
        preferred_format = SPA_VIDEO_FORMAT_BGRA;
    }
}

void VideoCapture::set_callback(
    VideoFrameCallback callback)
{
    frame_callback = std::move(callback);
}
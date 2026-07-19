#include <chrono>
#include <iostream>

#include <spa/param/audio/format-utils.h>
#include <pipewire/pipewire.h>

#include "AudioCapture.hpp"

static const pw_stream_events stream_events =
{
    PW_VERSION_STREAM_EVENTS,

    .state_changed =
        AudioCapture::on_stream_state_changed,

    .param_changed =
        AudioCapture::on_stream_param_changed,

    .process =
        AudioCapture::on_stream_process
};

bool AudioCapture::initialize()
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
            "Audio",
            PW_KEY_MEDIA_CATEGORY,
            "Capture",
            PW_KEY_MEDIA_ROLE,
            "Production", // or Music
            nullptr),
        &stream_events,
        this);

    initialized = true;

    std::cout << "Audio connected to PipeWire\n";

    return true;
}


void AudioCapture::update()
{
    if (!initialized)
        return;

    pw_loop_iterate(
        pw_main_loop_get_loop(loop),
        0);
}


void AudioCapture::shutdown()
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


AudioCapture::~AudioCapture()
{
    shutdown();
}



void AudioCapture::on_registry_global(
    void* data,
    uint32_t id,
    uint32_t permissions,
    const char* type,
    uint32_t version,
    const struct spa_dict* props)
{
    auto* self = static_cast<AudioCapture*>(data);


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

void AudioCapture::on_stream_state_changed(
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

void AudioCapture::on_stream_param_changed(
    void* data,
    uint32_t id,
    const struct spa_pod* param)
{
    auto* self = static_cast<AudioCapture*>(data);

    if (!param) return;
    if (spa_format_audio_raw_parse(
        param,
        &self->audio_format) < 0)
    {
        std::cout
            << "Could not parse audio format\n";

        return;
    }

    self->sample_rate = self->audio_format.rate;
    self->channels = self->audio_format.channels;

    std::cout
        << "Audio format "
        << self->sample_rate
        << "Hz "
        << self->channels
        << " channels\n";
}


static int64_t now_nanoseconds()
{
    return std::chrono::duration_cast<
        std::chrono::nanoseconds>(
            std::chrono::steady_clock::now()
                .time_since_epoch())
        .count();
}

void AudioCapture::on_stream_process(void* data)
{
    auto* self =
        static_cast<AudioCapture*>(data);

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

        uint32_t frames = spa_data_ptr->chunk->size / (sizeof(float) * self->channels);
        self->frame_callback(
            static_cast<float*>(spa_data_ptr->data),

            frames,
            self->channels,
            timestamp);
    }





    pw_stream_queue_buffer(
        self->stream,
        buffer);
}

bool AudioCapture::connect_to_node(
    uint32_t node_id)
{
    if (!stream)
        return false;

    uint8_t buffer[1024];
    spa_pod_builder builder =
        SPA_POD_BUILDER_INIT(
            buffer,
            sizeof(buffer));

    const spa_pod* params[1];

    struct spa_audio_info_raw info = SPA_AUDIO_INFO_RAW_INIT(
        .format = SPA_AUDIO_FORMAT_F32,
        .rate = preferred_sample_rate,
        .channels = preferred_channels
    );
    params[0] =
        spa_format_audio_raw_build(
            &builder,
            SPA_PARAM_EnumFormat,
            &info);

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
            << "Failed connecting audio stream: "
            << result
            << "\n";

        return false;
    }


    std::cout
        << "PipeWire stream connected\n";


    return true;
}

void AudioCapture::set_preferred_format(
    uint32_t rate,
    uint32_t channel_count)
{
    preferred_sample_rate = rate;
    preferred_channels = channel_count;
}

void AudioCapture::set_callback(AudioFrameCallback callback) {
    frame_callback = std::move(callback);
}
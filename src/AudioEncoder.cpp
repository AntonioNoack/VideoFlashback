#include "AudioEncoder.hpp"

#include <iostream>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
}

AudioEncoder::AudioEncoder(
    PacketBuffer& buffer)
    : ring(buffer)
{
}

AudioEncoder::~AudioEncoder()
{
    stop();
    if (codec) {
        avcodec_free_context(&codec);
    }
}

bool AudioEncoder::initialize(
    int sampleRate,
    int channels)
{
    const AVCodec* encoder =
        avcodec_find_encoder(
            AV_CODEC_ID_AAC);

    if(!encoder)
    {
        std::cerr
            << "AAC encoder unavailable\n";

        return false;
    }

    codec = avcodec_alloc_context3(encoder);
    if (!codec) {
        std::cerr << "Failed to allocate AAC codec context\n";
        return false;
    }

    codec->sample_rate = sampleRate;
    codec->sample_fmt = AV_SAMPLE_FMT_FLTP; // Planar float
    codec->bit_rate = 128000;
    codec->time_base = { 1, sampleRate };

    // Set channel layout using FFmpeg 6.x API
    av_channel_layout_default(&codec->ch_layout, channels);

    if (avcodec_open2(
            codec,
            encoder,
            nullptr) < 0)
    {
        std::cerr << "Failed to open AAC codec\n";
        avcodec_free_context(&codec);
        codec = nullptr;
        return false;
    }

    input_channels = channels;
    input_sample_rate = sampleRate;

    AudioInfo info;
    info.sampleRate = sampleRate;
    info.channels = channels;
    info.frame_size = codec->frame_size;
    info.time_base_num = codec->time_base.num;
    info.time_base_den = codec->time_base.den;

    if (codec->extradata &&
        codec->extradata_size > 0)
    {
        info.extradata.assign(
            codec->extradata,
            codec->extradata +
            codec->extradata_size);
    }

    ring.set_audio_info(info);

    start();
    return true;
}

void AudioEncoder::push(
    RawAudioFrame frame)
{
    {
        std::lock_guard lock(mutex);
        frames.push(std::move(frame));
    }
    condition.notify_one();
}

void AudioEncoder::thread_main()
{
    while(running)
    {
        RawAudioFrame frame;
        {
            std::unique_lock lock(mutex);

            condition.wait(
                lock,
                [&]
                {
                    return !frames.empty()
                           || !running;
                });

            if(!running)
                break;

            frame = std::move(frames.front());
            frames.pop();
        }

        if (first_timestamp_ns < 0) {
            first_timestamp_ns = frame.timestamp_ns;
        }

        // Buffer the samples
        sample_buffer.insert(
            sample_buffer.end(),
            frame.samples.begin(),
            frame.samples.end());

        int frame_size = codec->frame_size;
        int required_samples = frame_size * input_channels;

        while (sample_buffer.size() >= static_cast<size_t>(required_samples))
        {
            AVFrame* avframe = av_frame_alloc();
            if (!avframe) {
                std::cerr << "Failed to allocate AVFrame\n";
                break;
            }

            avframe->format = codec->sample_fmt;
            avframe->nb_samples = frame_size;
            avframe->sample_rate = codec->sample_rate;
            av_channel_layout_copy(&avframe->ch_layout, &codec->ch_layout);

            if (av_frame_get_buffer(avframe, 0) < 0) {
                std::cerr << "Failed to allocate audio frame data buffer\n";
                av_frame_free(&avframe);
                break;
            }

            // Convert interleaved float to planar float
            float* src = sample_buffer.data();
            float** dst = reinterpret_cast<float**>(avframe->data);

            for (int c = 0; c < input_channels; ++c) {
                for (int i = 0; i < frame_size; ++i) {
                    dst[c][i] = src[i * input_channels + c];
                }
            }

            avframe->pts = total_samples_sent;
            total_samples_sent += frame_size;

            // Remove processed samples from buffer
            sample_buffer.erase(
                sample_buffer.begin(),
                sample_buffer.begin() + required_samples);

            if (avcodec_send_frame(
                    codec,
                    avframe) >= 0)
            {
                AVPacket* packet = av_packet_alloc();
                if (!packet) {
                    av_frame_free(&avframe);
                    continue;
                }

                while(avcodec_receive_packet(
                        codec,
                        packet) == 0)
                {
                    EncodedPacket out;
                    out.type = StreamType::Audio;

                    out.data.assign(
                        packet->data,
                        packet->data + packet->size);

                    // Rescale to 90000 time base
                    out.pts = av_rescale_q(
                        packet->pts,
                        codec->time_base,
                        AVRational{1, 90000});
                    out.dts = av_rescale_q(
                        packet->dts,
                        codec->time_base,
                        AVRational{1, 90000});
                    out.keyframe = true; // Audio frames are keyframes

                    ring.push(std::move(out));

                    av_packet_unref(packet);
                }

                av_packet_free(&packet);
            }

            av_frame_free(&avframe);
        }
    }
}
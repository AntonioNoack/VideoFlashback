#include "VideoEncoder.hpp"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <string>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
#include <libavutil/opt.h>
#include <libavutil/mathematics.h>
#include <libavutil/pixfmt.h>
}

VideoEncoder::VideoEncoder(
    PacketBuffer& buffer)
    : ring(buffer)
{
}

VideoEncoder::~VideoEncoder()
{
    stop();
    if (codec) avcodec_free_context(&codec);
    if (scaler) sws_freeContext(scaler);
}

static std::string to_lower(std::string value)
{
    for (char& c : value)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return value;
}

static AVCodecID codec_id_from_name(const std::string& name)
{
    const std::string lower = to_lower(name);
    if (lower == "h264" || lower == "avc" || lower == "libx264")
        return AV_CODEC_ID_H264;
    if (lower == "hevc" || lower == "h265" || lower == "libx265")
        return AV_CODEC_ID_HEVC;
    return AV_CODEC_ID_NONE;
}

static AVPixelFormat av_pixel_format_from_name(const std::string& name)
{
    const std::string lower = to_lower(name);
    if (lower == "bgra")
        return AV_PIX_FMT_BGRA;
    if (lower == "rgba")
        return AV_PIX_FMT_RGBA;
    if (lower == "bgr0" || lower == "bgrx")
        return AV_PIX_FMT_BGR0;
    if (lower == "rgb0" || lower == "rgbx")
        return AV_PIX_FMT_RGB0;
    if (lower == "argb")
        return AV_PIX_FMT_ARGB;
    if (lower == "abgr")
        return AV_PIX_FMT_ABGR;
    return AV_PIX_FMT_NONE;
}

bool VideoEncoder::configure(const Config& config)
{
    settings = config;
    configured = true;

    if (settings.hw_encoder.empty() &&
        codec_id_from_name(settings.encoding) == AV_CODEC_ID_NONE)
    {
        std::cerr << "Unsupported encoding '" << settings.encoding
                  << "'; use h264 or hevc (or set hw_encoder)\n";
        return false;
    }

    if (av_pixel_format_from_name(settings.pixel_format) == AV_PIX_FMT_NONE)
    {
        std::cerr << "Unsupported pixel_format '" << settings.pixel_format
                  << "'; use bgra, rgba, bgr0, rgb0, argb, or abgr\n";
        return false;
    }

    start();
    return true;
}

bool VideoEncoder::ensure_encoder(int src_w, int src_h)
{
    if (encoder_ready)
    {
        if (src_w != source_width || src_h != source_height)
        {
            std::cerr << "Capture resolution changed from "
                      << source_width << "x" << source_height
                      << " to " << src_w << "x" << src_h
                      << "; ignoring frame\n";
            return false;
        }
        return true;
    }

    source_width = src_w;
    source_height = src_h;

    int out_w = src_w;
    int out_h = src_h;
    int scale_w = 0;
    int scale_h = 0;
    if (parse_scale(settings.scale, scale_w, scale_h))
    {
        out_w = scale_w;
        out_h = scale_h;
    }

    out_w = std::max(2, out_w & ~1);
    out_h = std::max(2, out_h & ~1);
    encode_width = out_w;
    encode_height = out_h;

    const AVCodec* encoder = nullptr;
    AVCodecID codec_id = AV_CODEC_ID_NONE;

    if (!settings.hw_encoder.empty())
    {
        encoder = avcodec_find_encoder_by_name(settings.hw_encoder.c_str());
        if (!encoder)
        {
            std::cerr << "Hardware encoder '" << settings.hw_encoder
                      << "' not found\n";
            return false;
        }
        codec_id = encoder->id;
    }
    else
    {
        codec_id = codec_id_from_name(settings.encoding);
        encoder = avcodec_find_encoder(codec_id);
        if (!encoder)
        {
            std::cerr << "Encoder unavailable for " << settings.encoding << "\n";
            return false;
        }
    }

    codec = avcodec_alloc_context3(encoder);
    if (!codec)
    {
        std::cerr << "Failed to allocate codec context\n";
        return false;
    }

    codec->width = encode_width;
    codec->height = encode_height;
    codec->time_base = { 1, 90000 };
    codec->framerate = { settings.capture_fps, 1 };
    codec->pix_fmt = AV_PIX_FMT_YUV420P;
    codec->max_b_frames = 0;
    codec->gop_size = std::max(
        1,
        static_cast<int>(settings.capture_fps * settings.keyframe_interval_sec + 0.5));

    const std::string rate_mode = to_lower(settings.rate_control);
    if (rate_mode == "crf")
    {
        codec->bit_rate = 0;
    }
    else
    {
        codec->bit_rate = settings.bitrate;
    }

    if (codec->priv_data)
    {
        if (!settings.preset.empty())
            av_opt_set(codec->priv_data, "preset", settings.preset.c_str(), 0);

        if (!settings.tune.empty())
            av_opt_set(codec->priv_data, "tune", settings.tune.c_str(), 0);

        if (rate_mode == "crf")
        {
            // Works for libx264/libx265 and several hw wrappers that accept crf.
            av_opt_set_int(codec->priv_data, "crf", settings.crf, 0);
        }
    }

    if (avcodec_open2(codec, encoder, nullptr) < 0)
    {
        std::cerr << "Failed to open video encoder"
                  << (settings.hw_encoder.empty() ? "" : (" " + settings.hw_encoder))
                  << "\n";
        avcodec_free_context(&codec);
        return false;
    }

    VideoInfo info;
    info.width = codec->width;
    info.height = codec->height;
    info.fps = settings.capture_fps;
    info.codec_id = static_cast<int>(codec_id);
    info.time_base_num = codec->time_base.num;
    info.time_base_den = codec->time_base.den;

    if (codec->extradata && codec->extradata_size > 0)
    {
        info.extradata.assign(
            codec->extradata,
            codec->extradata + codec->extradata_size);
    }

    ring.set_video_info(info);

    const AVPixelFormat src_fmt =
        av_pixel_format_from_name(settings.pixel_format);

    scaler = sws_getContext(
        source_width,
        source_height,
        src_fmt,
        encode_width,
        encode_height,
        AV_PIX_FMT_YUV420P,
        SWS_FAST_BILINEAR,
        nullptr,
        nullptr,
        nullptr);

    if (!scaler)
    {
        std::cerr << "Failed creating scaler\n";
        avcodec_free_context(&codec);
        return false;
    }

    encoder_ready = true;

    std::cout << "Video encoder ready: capture "
              << source_width << "x" << source_height
              << " (" << settings.pixel_format << ") -> encode "
              << encode_width << "x" << encode_height
              << " @ " << settings.capture_fps << " fps, "
              << (settings.hw_encoder.empty() ? settings.encoding : settings.hw_encoder)
              << ", rate_control=" << settings.rate_control;
    if (rate_mode == "crf")
        std::cout << " crf=" << settings.crf;
    else
        std::cout << " bitrate=" << (settings.bitrate / 1000) << " kbps";
    std::cout << ", gop=" << codec->gop_size << "\n";

    return true;
}

void VideoEncoder::push(RawVideoFrame frame)
{
    if (!configured)
        return;

    {
        std::lock_guard lock(mutex);

        if (settings.max_queue_frames > 0)
        {
            while (static_cast<int>(frames.size()) >= settings.max_queue_frames)
                frames.pop();
        }

        frames.push(std::move(frame));
    }

    condition.notify_one();
}

static AVFrame* create_yuv_frame(AVCodecContext* codec)
{
    AVFrame* frame = av_frame_alloc();
    frame->format = codec->pix_fmt;
    frame->width = codec->width;
    frame->height = codec->height;

    av_frame_get_buffer(frame, 32);

    return frame;
}

void VideoEncoder::thread_main()
{
    while (running)
    {
        RawVideoFrame frame;
        {
            std::unique_lock lock(mutex);

            condition.wait(
                lock,
                [&]
                {
                    return !frames.empty() || !running;
                });

            if (!running)
                break;

            frame = std::move(frames.front());
            frames.pop();
        }

        if (frame.width <= 0 || frame.height <= 0)
            continue;

        const int64_t min_interval_ns =
            1'000'000'000LL / std::max(1, settings.capture_fps);
        const int64_t ts = static_cast<int64_t>(frame.timestamp_ns);
        if (last_encoded_timestamp_ns >= 0 &&
            ts - last_encoded_timestamp_ns < min_interval_ns)
        {
            continue;
        }

        if (!ensure_encoder(frame.width, frame.height))
            continue;

        AVFrame* avframe = create_yuv_frame(codec);
        if (!avframe)
            continue;

        uint8_t* src[] = { frame.data.data() };
        int src_stride[] = { frame.stride };

        sws_scale(
            scaler,
            src,
            src_stride,
            0,
            frame.height,
            avframe->data,
            avframe->linesize);

        if (first_timestamp_ns < 0)
            first_timestamp_ns = ts;

        avframe->pts = av_rescale_q(
            ts - first_timestamp_ns,
            AVRational{1, 1000000000},
            codec->time_base);

        if (avcodec_send_frame(codec, avframe) >= 0)
        {
            AVPacket* packet = av_packet_alloc();
            if (!packet)
            {
                av_frame_free(&avframe);
                continue;
            }

            while (avcodec_receive_packet(codec, packet) == 0)
            {
                EncodedPacket out;
                out.type = StreamType::Video;
                out.data.assign(
                    packet->data,
                    packet->data + packet->size);
                out.pts = packet->pts;
                out.dts = packet->dts;
                out.keyframe = packet->flags & AV_PKT_FLAG_KEY;

                ring.push(std::move(out));
                av_packet_unref(packet);
            }

            av_packet_free(&packet);
            last_encoded_timestamp_ns = ts;
        }

        av_frame_free(&avframe);
    }
}

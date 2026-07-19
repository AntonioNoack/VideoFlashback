#include "VideoEncoder.hpp"

#include <algorithm>
#include <cctype>
#include <dlfcn.h>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
#include <libavutil/imgutils.h>
#include <libavutil/log.h>
#include <libavutil/opt.h>
#include <libavutil/mathematics.h>
#include <libavutil/pixfmt.h>
#include <libswscale/swscale.h>
}

namespace fs = std::filesystem;

VideoEncoder::VideoEncoder(PacketBuffer& buffer)
    : ring(buffer)
{
}

void VideoEncoder::release_encoder()
{
    if (codec)
        avcodec_free_context(&codec);
    if (scaler)
    {
        sws_freeContext(scaler);
        scaler = nullptr;
    }
    if (hw_frames_ctx)
    {
        av_buffer_unref(&hw_frames_ctx);
        hw_frames_ctx = nullptr;
    }
    if (hw_device_ctx)
    {
        av_buffer_unref(&hw_device_ctx);
        hw_device_ctx = nullptr;
    }
    use_vaapi = false;
    encoder_ready = false;
}

VideoEncoder::~VideoEncoder()
{
    stop();
    release_encoder();
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

static bool force_software_encoder(const std::string& hw_encoder)
{
    const std::string lower = to_lower(hw_encoder);
    return lower == "software" || lower == "none" || lower == "off";
}

static bool auto_select_encoder(const std::string& hw_encoder)
{
    const std::string lower = to_lower(hw_encoder);
    return lower.empty() || lower == "auto";
}

static bool is_nvenc_encoder(const std::string& name)
{
    return to_lower(name).find("nvenc") != std::string::npos;
}

static bool is_vaapi_encoder(const std::string& name)
{
    return to_lower(name).find("vaapi") != std::string::npos;
}

static bool is_amf_encoder(const std::string& name)
{
    return to_lower(name).find("amf") != std::string::npos;
}

static bool is_x264_style_encoder(const std::string& name)
{
    const std::string lower = to_lower(name);
    return lower.find("libx264") != std::string::npos ||
           lower.find("libx265") != std::string::npos;
}

static bool cuda_runtime_available()
{
    void* handle = dlopen("libcuda.so.1", RTLD_LAZY | RTLD_LOCAL);
    if (!handle)
        return false;
    dlclose(handle);
    return true;
}

static bool vaapi_device_available()
{
    std::error_code ec;
    if (!fs::exists("/dev/dri", ec))
        return false;

    for (const auto& entry : fs::directory_iterator("/dev/dri", ec))
    {
        const std::string name = entry.path().filename().string();
        if (name.rfind("renderD", 0) == 0)
            return true;
    }
    return false;
}

// Prefer VAAPI on Linux AMD/Intel (e.g. 7900 XTX), then NVENC if CUDA exists.
static std::vector<std::string> hardware_encoder_candidates(const std::string& encoding)
{
    std::vector<std::string> out;
    const bool hevc = codec_id_from_name(encoding) == AV_CODEC_ID_HEVC;

    if (vaapi_device_available())
        out.push_back(hevc ? "hevc_vaapi" : "h264_vaapi");

    if (cuda_runtime_available())
        out.push_back(hevc ? "hevc_nvenc" : "h264_nvenc");

    // AMF is mainly useful on Windows; keep as a last HW try.
    out.push_back(hevc ? "hevc_amf" : "h264_amf");
    return out;
}

static std::string map_nvenc_preset(const std::string& preset)
{
    const std::string lower = to_lower(preset);
    if (lower.empty())
        return "p4";
    if (lower.size() == 2 && lower[0] == 'p' && lower[1] >= '1' && lower[1] <= '7')
        return lower;
    if (lower == "ultrafast" || lower == "superfast" || lower == "veryfast")
        return "p1";
    if (lower == "faster" || lower == "fast")
        return "p4";
    if (lower == "medium")
        return "p5";
    if (lower == "slow" || lower == "slower" || lower == "veryslow")
        return lower == "slow" ? "p6" : "p7";
    return "p4";
}

static std::string map_nvenc_tune(const std::string& tune)
{
    const std::string lower = to_lower(tune);
    if (lower.empty())
        return {};
    if (lower == "zerolatency" || lower == "ll" || lower == "lowlatency")
        return "ll";
    if (lower == "ull" || lower == "ultralowlatency")
        return "ull";
    if (lower == "hq" || lower == "highquality")
        return "hq";
    return {};
}

struct QuietFFmpegLogs
{
    int previous;
    explicit QuietFFmpegLogs(bool enable)
        : previous(av_log_get_level())
    {
        if (enable)
            av_log_set_level(AV_LOG_QUIET);
    }
    ~QuietFFmpegLogs()
    {
        av_log_set_level(previous);
    }
};

static enum AVPixelFormat get_vaapi_format(
    AVCodecContext*,
    const enum AVPixelFormat* pix_fmts)
{
    for (const enum AVPixelFormat* p = pix_fmts; *p != AV_PIX_FMT_NONE; ++p)
    {
        if (*p == AV_PIX_FMT_VAAPI)
            return *p;
    }
    return AV_PIX_FMT_NONE;
}

static void apply_encoder_private_options(
    AVCodecContext* ctx,
    const std::string& encoder_name,
    const Config& settings,
    const std::string& rate_mode)
{
    if (!ctx->priv_data)
        return;

    if (is_x264_style_encoder(encoder_name))
    {
        if (!settings.preset.empty())
            av_opt_set(ctx->priv_data, "preset", settings.preset.c_str(), 0);
        if (!settings.tune.empty())
            av_opt_set(ctx->priv_data, "tune", settings.tune.c_str(), 0);
        if (rate_mode == "crf")
            av_opt_set_int(ctx->priv_data, "crf", settings.crf, 0);
        return;
    }

    if (is_nvenc_encoder(encoder_name))
    {
        av_opt_set(ctx->priv_data, "preset", map_nvenc_preset(settings.preset).c_str(), 0);
        const std::string tune = map_nvenc_tune(settings.tune);
        if (!tune.empty())
            av_opt_set(ctx->priv_data, "tune", tune.c_str(), 0);
        if (rate_mode == "crf")
            av_opt_set_int(ctx->priv_data, "cq", settings.crf, 0);
        return;
    }

    if (is_vaapi_encoder(encoder_name))
    {
        // Low-latency-ish: smaller packed headers; bitrate mode uses rc_mode=CBR.
        if (rate_mode == "crf")
            av_opt_set(ctx->priv_data, "rc_mode", "CQP", 0);
        else
            av_opt_set(ctx->priv_data, "rc_mode", "CBR", 0);
        return;
    }

    if (is_amf_encoder(encoder_name))
    {
        if (to_lower(settings.tune).find("latency") != std::string::npos ||
            to_lower(settings.tune) == "zerolatency")
        {
            av_opt_set(ctx->priv_data, "usage", "ultralowlatency", 0);
        }
    }
}

struct OpenedEncoder
{
    AVCodecContext* codec = nullptr;
    AVBufferRef* hw_device_ctx = nullptr;
    AVBufferRef* hw_frames_ctx = nullptr;
    AVCodecID codec_id = AV_CODEC_ID_NONE;
    bool use_vaapi = false;
};

static void free_opened_encoder(OpenedEncoder& opened)
{
    if (opened.codec)
        avcodec_free_context(&opened.codec);
    if (opened.hw_frames_ctx)
        av_buffer_unref(&opened.hw_frames_ctx);
    if (opened.hw_device_ctx)
        av_buffer_unref(&opened.hw_device_ctx);
    opened = {};
}

static bool try_open_vaapi_encoder(
    const char* name,
    int width,
    int height,
    const Config& settings,
    const std::string& rate_mode,
    OpenedEncoder& out,
    bool quiet)
{
    QuietFFmpegLogs silence(quiet);

    const AVCodec* encoder = avcodec_find_encoder_by_name(name);
    if (!encoder)
        return false;

    AVBufferRef* device_ctx = nullptr;
    if (av_hwdevice_ctx_create(
            &device_ctx,
            AV_HWDEVICE_TYPE_VAAPI,
            nullptr,
            nullptr,
            0) < 0)
    {
        return false;
    }

    AVBufferRef* frames_ref = av_hwframe_ctx_alloc(device_ctx);
    if (!frames_ref)
    {
        av_buffer_unref(&device_ctx);
        return false;
    }

    auto* frames = reinterpret_cast<AVHWFramesContext*>(frames_ref->data);
    frames->format = AV_PIX_FMT_VAAPI;
    frames->sw_format = AV_PIX_FMT_NV12;
    frames->width = width;
    frames->height = height;
    frames->initial_pool_size = 16;

    if (av_hwframe_ctx_init(frames_ref) < 0)
    {
        av_buffer_unref(&frames_ref);
        av_buffer_unref(&device_ctx);
        return false;
    }

    AVCodecContext* ctx = avcodec_alloc_context3(encoder);
    if (!ctx)
    {
        av_buffer_unref(&frames_ref);
        av_buffer_unref(&device_ctx);
        return false;
    }

    ctx->width = width;
    ctx->height = height;
    ctx->time_base = { 1, 90000 };
    ctx->framerate = { settings.capture_fps, 1 };
    ctx->pix_fmt = AV_PIX_FMT_VAAPI;
    ctx->max_b_frames = 0;
    ctx->gop_size = std::max(
        1,
        static_cast<int>(settings.capture_fps * settings.keyframe_interval_sec + 0.5));
    ctx->get_format = get_vaapi_format;
    ctx->hw_device_ctx = av_buffer_ref(device_ctx);
    ctx->hw_frames_ctx = av_buffer_ref(frames_ref);

    if (rate_mode == "crf")
    {
        ctx->bit_rate = 0;
        ctx->global_quality = settings.crf * FF_QP2LAMBDA;
    }
    else
    {
        ctx->bit_rate = settings.bitrate;
    }

    apply_encoder_private_options(ctx, name, settings, rate_mode);

    if (avcodec_open2(ctx, encoder, nullptr) < 0)
    {
        avcodec_free_context(&ctx);
        av_buffer_unref(&frames_ref);
        av_buffer_unref(&device_ctx);
        return false;
    }

    out.codec = ctx;
    out.hw_device_ctx = device_ctx;
    out.hw_frames_ctx = frames_ref;
    out.codec_id = encoder->id;
    out.use_vaapi = true;
    return true;
}

static bool try_open_software_style_encoder(
    const char* name,
    int width,
    int height,
    const Config& settings,
    const std::string& rate_mode,
    OpenedEncoder& out,
    bool quiet)
{
    QuietFFmpegLogs silence(quiet);

    const AVCodec* encoder = avcodec_find_encoder_by_name(name);
    if (!encoder)
        return false;

    AVCodecContext* ctx = avcodec_alloc_context3(encoder);
    if (!ctx)
        return false;

    ctx->width = width;
    ctx->height = height;
    ctx->time_base = { 1, 90000 };
    ctx->framerate = { settings.capture_fps, 1 };
    ctx->pix_fmt = AV_PIX_FMT_YUV420P;
    ctx->max_b_frames = 0;
    ctx->gop_size = std::max(
        1,
        static_cast<int>(settings.capture_fps * settings.keyframe_interval_sec + 0.5));

    if (rate_mode == "crf")
        ctx->bit_rate = 0;
    else
        ctx->bit_rate = settings.bitrate;

    apply_encoder_private_options(ctx, name, settings, rate_mode);

    if (avcodec_open2(ctx, encoder, nullptr) < 0)
    {
        avcodec_free_context(&ctx);
        return false;
    }

    out.codec = ctx;
    out.codec_id = encoder->id;
    out.use_vaapi = false;
    return true;
}

static bool try_open_named_encoder(
    const char* name,
    int width,
    int height,
    const Config& settings,
    const std::string& rate_mode,
    OpenedEncoder& out,
    bool quiet)
{
    if (is_vaapi_encoder(name))
        return try_open_vaapi_encoder(name, width, height, settings, rate_mode, out, quiet);

    return try_open_software_style_encoder(
        name, width, height, settings, rate_mode, out, quiet);
}

static bool try_open_software_encoder(
    const std::string& encoding,
    int width,
    int height,
    const Config& settings,
    const std::string& rate_mode,
    OpenedEncoder& out)
{
    const AVCodecID codec_id = codec_id_from_name(encoding);
    if (codec_id == AV_CODEC_ID_NONE)
        return false;

    const AVCodec* encoder = avcodec_find_encoder(codec_id);
    if (!encoder)
        return false;

    return try_open_software_style_encoder(
        encoder->name, width, height, settings, rate_mode, out, false);
}

bool VideoEncoder::configure(const Config& config)
{
    settings = config;
    configured = true;

    if (!auto_select_encoder(settings.hw_encoder) &&
        !force_software_encoder(settings.hw_encoder))
    {
        if (!avcodec_find_encoder_by_name(settings.hw_encoder.c_str()))
        {
            std::cerr << "Hardware encoder '" << settings.hw_encoder
                      << "' is not available in this FFmpeg build\n";
            return false;
        }
    }
    else if (codec_id_from_name(settings.encoding) == AV_CODEC_ID_NONE)
    {
        std::cerr << "Unsupported encoding '" << settings.encoding
                  << "'; use h264 or hevc\n";
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

    const std::string rate_mode = to_lower(settings.rate_control);
    OpenedEncoder opened;
    std::string selected_name;

    if (!auto_select_encoder(settings.hw_encoder) &&
        !force_software_encoder(settings.hw_encoder))
    {
        selected_name = settings.hw_encoder;
        if (!try_open_named_encoder(
                settings.hw_encoder.c_str(),
                encode_width,
                encode_height,
                settings,
                rate_mode,
                opened,
                false))
        {
            std::cerr << "Failed to open hardware encoder '"
                      << settings.hw_encoder << "'\n";
            return false;
        }
    }
    else if (auto_select_encoder(settings.hw_encoder))
    {
        const auto candidates = hardware_encoder_candidates(settings.encoding);
        if (!candidates.empty())
            std::cout << "Probing hardware encoders...\n";

        for (const std::string& name : candidates)
        {
            if (!avcodec_find_encoder_by_name(name.c_str()))
                continue;

            OpenedEncoder candidate;
            if (try_open_named_encoder(
                    name.c_str(),
                    encode_width,
                    encode_height,
                    settings,
                    rate_mode,
                    candidate,
                    true))
            {
                opened = candidate;
                selected_name = name;
                std::cout << "Using hardware encoder: " << name << "\n";
                break;
            }
        }

        if (!opened.codec)
        {
            if (!try_open_software_encoder(
                    settings.encoding,
                    encode_width,
                    encode_height,
                    settings,
                    rate_mode,
                    opened))
            {
                std::cerr << "No usable hardware or software encoder for "
                          << settings.encoding << "\n";
                return false;
            }
            selected_name =
                opened.codec->codec ? opened.codec->codec->name : settings.encoding;
            std::cout << "Using software encoder: " << selected_name << "\n";
        }
    }
    else
    {
        if (!try_open_software_encoder(
                settings.encoding,
                encode_width,
                encode_height,
                settings,
                rate_mode,
                opened))
        {
            std::cerr << "Software encoder unavailable for "
                      << settings.encoding << "\n";
            return false;
        }
        selected_name =
            opened.codec->codec ? opened.codec->codec->name : settings.encoding;
    }

    codec = opened.codec;
    hw_device_ctx = opened.hw_device_ctx;
    hw_frames_ctx = opened.hw_frames_ctx;
    use_vaapi = opened.use_vaapi;

    VideoInfo info;
    info.width = codec->width;
    info.height = codec->height;
    info.fps = settings.capture_fps;
    info.codec_id = static_cast<int>(opened.codec_id);
    info.time_base_num = codec->time_base.num;
    info.time_base_den = codec->time_base.den;

    if (codec->extradata && codec->extradata_size > 0)
    {
        info.extradata.assign(
            codec->extradata,
            codec->extradata + codec->extradata_size);
    }

    ring.set_video_info(info);

    const AVPixelFormat src_fmt = av_pixel_format_from_name(settings.pixel_format);
    const AVPixelFormat dst_fmt = use_vaapi ? AV_PIX_FMT_NV12 : AV_PIX_FMT_YUV420P;

    scaler = sws_getContext(
        source_width,
        source_height,
        src_fmt,
        encode_width,
        encode_height,
        dst_fmt,
        SWS_FAST_BILINEAR,
        nullptr,
        nullptr,
        nullptr);

    if (!scaler)
    {
        std::cerr << "Failed creating scaler\n";
        release_encoder();
        return false;
    }

    encoder_ready = true;

    std::cout << "Video encoder ready: capture "
              << source_width << "x" << source_height
              << " (" << settings.pixel_format << ") -> encode "
              << encode_width << "x" << encode_height
              << " @ " << settings.capture_fps << " fps, "
              << selected_name
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

static AVFrame* create_sw_frame(AVCodecContext* codec, bool vaapi)
{
    AVFrame* frame = av_frame_alloc();
    frame->format = vaapi ? AV_PIX_FMT_NV12 : codec->pix_fmt;
    frame->width = codec->width;
    frame->height = codec->height;

    if (av_frame_get_buffer(frame, 32) < 0)
    {
        av_frame_free(&frame);
        return nullptr;
    }

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

        AVFrame* swframe = create_sw_frame(codec, use_vaapi);
        if (!swframe)
            continue;

        uint8_t* src[] = { frame.data.data() };
        int src_stride[] = { frame.stride };

        sws_scale(
            scaler,
            src,
            src_stride,
            0,
            frame.height,
            swframe->data,
            swframe->linesize);

        if (first_timestamp_ns < 0)
            first_timestamp_ns = ts;

        const int64_t pts = av_rescale_q(
            ts - first_timestamp_ns,
            AVRational{1, 1000000000},
            codec->time_base);

        AVFrame* send_frame = swframe;
        AVFrame* hwframe = nullptr;

        if (use_vaapi)
        {
            hwframe = av_frame_alloc();
            if (!hwframe ||
                av_hwframe_get_buffer(codec->hw_frames_ctx, hwframe, 0) < 0 ||
                av_hwframe_transfer_data(hwframe, swframe, 0) < 0)
            {
                std::cerr << "VAAPI frame upload failed\n";
                av_frame_free(&hwframe);
                av_frame_free(&swframe);
                continue;
            }
            hwframe->pts = pts;
            send_frame = hwframe;
            av_frame_free(&swframe);
        }
        else
        {
            swframe->pts = pts;
        }

        if (avcodec_send_frame(codec, send_frame) >= 0)
        {
            AVPacket* packet = av_packet_alloc();
            if (!packet)
            {
                av_frame_free(&send_frame);
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

        av_frame_free(&send_frame);
    }
}

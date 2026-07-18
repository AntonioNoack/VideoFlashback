#include "Encoder.hpp"

#include <iostream>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
#include <libavutil/opt.h>
}

Encoder::Encoder(
    RingBuffer& buffer)
    :
    ring(buffer)
{
}

Encoder::~Encoder()
{
    running = false;

    condition.notify_all();

    if (thread.joinable()) thread.join();
    if (codec) avcodec_free_context(&codec);
    if (scaler) sws_freeContext(scaler);
   
}

bool Encoder::initialize(
    int width,
    int height)
{
    const AVCodec* encoder =
        avcodec_find_encoder(
            AV_CODEC_ID_H264);

    if(!encoder)
    {
        std::cerr
            << "H264 encoder unavailable\n";

        return false;
    }


    codec = avcodec_alloc_context3(encoder);
    codec->width = width;
    codec->height = height;
    codec->time_base = { 1, 90000 };
    codec->framerate = { 60, 1 };

    codec->pix_fmt = AV_PIX_FMT_YUV420P;
    codec->bit_rate = 12000000;

    av_opt_set(
        codec->priv_data,
        "preset",
        "veryfast",
        0);

    av_opt_set(
        codec->priv_data,
        "tune",
        "zerolatency",
        0);

    codec->gop_size = 60;
    codec->max_b_frames = 0;

    avcodec_open2(
        codec,
        encoder,
        nullptr);

    VideoInfo info;
    info.width = codec->width;
    info.height = codec->height;
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

    ring.set_video_info(info);

    scaler = sws_getContext(
            width,
            height,
            AV_PIX_FMT_BGRA,

            width,
            height,
            AV_PIX_FMT_YUV420P,

            SWS_FAST_BILINEAR,

            nullptr,
            nullptr,
            nullptr);

    if (!scaler) {
        std::cerr
            << "Failed creating scaler\n";

        return false;
    }

    running = true;
    thread = std::thread(
            &Encoder::thread_main,
            this);

    return true;
}

void Encoder::push(
    RawFrame frame)
{
    {
        std::lock_guard lock(mutex);

        frames.push(std::move(frame));
    }

    condition.notify_one();
}

static AVFrame* create_yuv_frame(
    AVCodecContext* codec)
{
    AVFrame* frame = av_frame_alloc();
    frame->format = codec->pix_fmt;
    frame->width = codec->width;
    frame->height = codec->height;

    av_frame_get_buffer(frame, 32);

    return frame;
}

void Encoder::thread_main()
{
    while(running)
    {
        RawFrame frame;
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


        AVFrame* avframe = create_yuv_frame(codec);

        uint8_t* src[] = {
            frame.data.data()
        };


        int src_stride[] = {
            frame.stride
        };

        sws_scale(
            scaler,

            src,
            src_stride,

            0,
            frame.height,

            avframe->data,
            avframe->linesize);

        // 60 fps-hack, timebase is 90kHz
        avframe->pts = 1500 * frame_number++;

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
        }

        av_frame_free(&avframe);

        if (frame_number % 10 == 0) {
            std::cout
                << "Encoding frame "
                << frame_number
                << ": "
                << frame.width
                << "x"
                << frame.height
                << "\n";
        }
    }
}
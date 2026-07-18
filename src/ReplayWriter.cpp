#include "ReplayWriter.hpp"

#include <iostream>
#include <cstring>
#include <algorithm>

extern "C"
{
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
}



bool ReplayWriter::write(
    const std::string& filename,
    PacketBuffer& ring)
{

    auto packets = ring.snapshot();

    AudioInfo audioInfo = ring.get_audio_info();
    VideoInfo videoInfo = ring.get_video_info();

    if (packets.empty())
    {
        std::cerr
            << "No packets to write\n";

        return false;
    }


    AVFormatContext* format = nullptr;


    if (avformat_alloc_output_context2(
            &format,
            nullptr,
            "mp4",
            filename.c_str()) < 0)
    {
        std::cerr
            << "Could not create output context\n";

        return false;
    }


    AVStream* video_stream =
        avformat_new_stream(
            format,
            nullptr);

    if (!video_stream)
    {
        avformat_free_context(format);
        return false;
    }

    video_stream->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    video_stream->codecpar->codec_id = AV_CODEC_ID_H264;
    video_stream->codecpar->width = videoInfo.width;
    video_stream->codecpar->height = videoInfo.height;
    video_stream->codecpar->format = AV_PIX_FMT_YUV420P;

    video_stream->time_base =
    {
        videoInfo.time_base_num,
        videoInfo.time_base_den
    };

    video_stream->avg_frame_rate = { 60, 1 };
    video_stream->r_frame_rate = { 60, 1 };

    if (!videoInfo.extradata.empty())
    {
        video_stream->codecpar->extradata =
            static_cast<uint8_t*>(
                av_malloc(
                    videoInfo.extradata.size()));

        memcpy(
            video_stream->codecpar->extradata,
            videoInfo.extradata.data(),
            videoInfo.extradata.size());

        video_stream->codecpar->extradata_size =
            videoInfo.extradata.size();
    }

    AVStream* audio_stream = nullptr;
    if (audioInfo.sampleRate > 0)
    {
        audio_stream =
            avformat_new_stream(
                format,
                nullptr);

        if (!audio_stream)
        {
            avformat_free_context(format);
            return false;
        }

        audio_stream->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
        audio_stream->codecpar->codec_id = AV_CODEC_ID_AAC;
        audio_stream->codecpar->sample_rate = audioInfo.sampleRate;
        audio_stream->codecpar->frame_size = audioInfo.frame_size;
        av_channel_layout_default(&audio_stream->codecpar->ch_layout, audioInfo.channels);

        audio_stream->time_base =
        {
            audioInfo.time_base_num,
            audioInfo.time_base_den
        };

        if (!audioInfo.extradata.empty())
        {
            audio_stream->codecpar->extradata =
                static_cast<uint8_t*>(
                    av_malloc(
                        audioInfo.extradata.size()));

            memcpy(
                audio_stream->codecpar->extradata,
                audioInfo.extradata.data(),
                audioInfo.extradata.size());

            audio_stream->codecpar->extradata_size =
                audioInfo.extradata.size();
        }
    }

    if (!(format->oformat->flags &
          AVFMT_NOFILE))
    {
        if (avio_open(
                &format->pb,
                filename.c_str(),
                AVIO_FLAG_WRITE) < 0)
        {
            avformat_free_context(format);
            return false;
        }
    }


    if (avformat_write_header(
            format,
            nullptr) < 0)
    {
        std::cerr
            << "Failed writing header\n";

        avio_closep(
            &format->pb);

        avformat_free_context(format);

        return false;
    }

    av_dump_format(
        format,
        0,
        filename.c_str(),
        1);


    int64_t index = 0;


    int64_t first_pts = AV_NOPTS_VALUE;
    int64_t first_dts = AV_NOPTS_VALUE;


    for (const auto& input : packets)
    {
        AVStream* target_stream = nullptr;
        if (input.type == StreamType::Video)
        {
            target_stream = video_stream;
        }
        else if (input.type == StreamType::Audio && audio_stream != nullptr)
        {
            target_stream = audio_stream;
        }
        else
        {
            continue; // Skip unknown stream types or if audio is disabled
        }

        AVPacket* packet = av_packet_alloc();

        packet->data = const_cast<uint8_t*>(input.data.data());
        packet->size = input.data.size();
        packet->stream_index = target_stream->index;

        if (first_pts == AV_NOPTS_VALUE)
        {
            first_pts = input.pts;
            first_dts = input.dts;
        }

        int64_t relative_pts = input.pts - first_pts;
        int64_t relative_dts = (input.dts == AV_NOPTS_VALUE) ? relative_pts : (input.dts - first_dts);

        // Rescale PTS/DTS from unified 90000 timebase to target stream timebase
        packet->pts = av_rescale_q(
            relative_pts,
            AVRational{1, 90000},
            target_stream->time_base);

        packet->dts = av_rescale_q(
            relative_dts,
            AVRational{1, 90000},
            target_stream->time_base);

        if (input.type == StreamType::Video)
        {
            packet->duration = av_rescale_q(
                1,
                AVRational{1,60},
                target_stream->time_base);
        }
        else
        {
            packet->duration = 0;
        }

        if (input.keyframe)
        {
            packet->flags |=
                AV_PKT_FLAG_KEY;
        }


        if (av_interleaved_write_frame(
                format,
                packet) < 0)
        {
            std::cerr
                << "Packet write failed\n";
        }

        index++;
        av_packet_free(&packet);
    }


    av_write_trailer(format);

    if (!(format->oformat->flags &
          AVFMT_NOFILE))
    {
        avio_closep(
            &format->pb);
    }


    avformat_free_context(format);


    std::cout
        << "Saved "
        << index
        << " packets to "
        << filename
        << "\n";


    return true;
}
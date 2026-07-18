#include "ReplayWriter.hpp"

#include <iostream>
#include <cstring>

extern "C"
{
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}



bool ReplayWriter::write(
    const std::string& filename,
    PacketBuffer& ring)
{

    auto packets = ring.snapshot();

    AudioInfo AudioInfo = ring.get_audio_info();
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


    AVStream* stream =
        avformat_new_stream(
            format,
            nullptr);

    if (!stream)
    {
        avformat_free_context(format);
        return false;
    }


    /*
       We do not have codec parameters stored yet.
       This is temporarily filled with H264.
    */

    stream->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    stream->codecpar->codec_id = AV_CODEC_ID_H264;
    stream->codecpar->width = videoInfo.width;
    stream->codecpar->height = videoInfo.height;
    stream->codecpar->format = AV_PIX_FMT_YUV420P;

    stream->time_base =
    {
        videoInfo.time_base_num,
        videoInfo.time_base_den
    };

    stream->avg_frame_rate = { 60, 1 };
    stream->r_frame_rate = { 60, 1 };

    if (!videoInfo.extradata.empty())
    {
        stream->codecpar->extradata =
            static_cast<uint8_t*>(
                av_malloc(
                    videoInfo.extradata.size()));

        memcpy(
            stream->codecpar->extradata,
            videoInfo.extradata.data(),
            videoInfo.extradata.size());

        stream->codecpar->extradata_size =
            videoInfo.extradata.size();
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
        AVPacket* packet = av_packet_alloc();

        packet->data = const_cast<uint8_t*>(input.data.data());
        packet->size = input.data.size();
        packet->stream_index = stream->index;

        if (first_pts == AV_NOPTS_VALUE)
        {
            first_pts = input.pts;
            first_dts = input.dts;
        }

        packet->pts = input.pts - first_pts;
        packet->dts = input.dts - first_dts;

        packet->duration = av_rescale_q(
            1,
            AVRational{1,60},
            stream->time_base);

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
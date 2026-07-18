#pragma once

#include <deque>
#include <mutex>
#include <vector>
#include <cstdint>

struct VideoInfo
{
    int width = 0;
    int height = 0;

    int time_base_num = 1;
    int time_base_den = 90000;

    std::vector<uint8_t> extradata;
};

struct AudioInfo
{
    int sampleRate = 48000;
    int channels = 2;

    int time_base_num = 1;
    int time_base_den = 90000;

    std::vector<uint8_t> extradata;
};

enum class StreamType
{
    Video,
    Audio
};


struct EncodedPacket
{
    StreamType type;
    std::vector<uint8_t> data;

    int64_t pts = 0;
    int64_t dts = 0;

    bool keyframe = false;
};


class PacketBuffer
{
public:

    explicit PacketBuffer(int64_t duration_seconds);

    void push(EncodedPacket packet);

    void set_audio_info(const AudioInfo& info);
    void set_video_info(const VideoInfo& info);

    AudioInfo get_audio_info();
    VideoInfo get_video_info();

    std::vector<EncodedPacket> snapshot();


private:

    void trim();

private:

    std::mutex mutex;

    std::deque<EncodedPacket> packets;

    AudioInfo audio_info;
    VideoInfo video_info;

    int64_t duration;
    int64_t newest_pts = 0;
    int64_t time_base = 90000;
};
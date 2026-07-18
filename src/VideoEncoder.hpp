#pragma once

#include "PacketBuffer.hpp"
#include "EncoderWorker.hpp"

#include <thread>
#include <mutex>
#include <queue>
#include <condition_variable>


struct AVCodecContext;
struct SwsContext;


struct RawVideoFrame
{
    std::vector<uint8_t> data;

    int width;
    int height;

    int stride;
    uint64_t timestamp_ns;
};


class VideoEncoder :
    public EncoderWorker
{
public:

    VideoEncoder(PacketBuffer& buffer);
    ~VideoEncoder();

    bool initialize(int width, int height);
    void push(RawVideoFrame frame);

    void set_video_info();

private:

    void thread_main();
    void update_video_info();

private:

    PacketBuffer& ring;

    std::queue<RawVideoFrame> frames;

    AVCodecContext* codec = nullptr;
    SwsContext* scaler = nullptr;

    int64_t first_timestamp_ns = -1;

};
#pragma once

#include "PacketBuffer.hpp"
#include "EncoderWorker.hpp"
#include "Config.hpp"

#include <thread>
#include <mutex>
#include <queue>
#include <condition_variable>
#include <vector>


struct AVCodecContext;
struct AVBufferRef;
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

    // Apply encode settings. Opens the worker thread; the codec/scaler are
    // created lazily from the first captured frame's resolution.
    bool configure(const Config& config);
    void push(RawVideoFrame frame);

private:

    void thread_main() override;
    bool ensure_encoder(int source_width, int source_height);
    void release_encoder();

private:

    PacketBuffer& ring;

    std::queue<RawVideoFrame> frames;

    AVCodecContext* codec = nullptr;
    SwsContext* scaler = nullptr;
    AVBufferRef* hw_device_ctx = nullptr;
    AVBufferRef* hw_frames_ctx = nullptr;

    Config settings;
    bool configured = false;
    bool encoder_ready = false;
    bool use_vaapi = false;

    int source_width = 0;
    int source_height = 0;
    int encode_width = 0;
    int encode_height = 0;

    int64_t first_timestamp_ns = -1;
    int64_t last_encoded_timestamp_ns = -1;
};

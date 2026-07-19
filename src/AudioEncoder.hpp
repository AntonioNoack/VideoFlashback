#pragma once

#include "PacketBuffer.hpp"
#include "EncoderWorker.hpp"

#include <thread>
#include <mutex>
#include <queue>
#include <condition_variable>
#include <vector>

struct AVCodecContext;

struct RawAudioFrame
{
    std::vector<float> samples;

    int sampleRate;
    int channels;

    int stride;
    uint64_t timestamp_ns;
};

class AudioEncoder :
    public EncoderWorker
{
public:
    AudioEncoder(PacketBuffer& buffer);
    ~AudioEncoder();

    bool initialize(int sampleRate, int channels, int64_t bitrate = 128000);
    void push(RawAudioFrame frame);

private:
    void thread_main() override;

private:
    PacketBuffer& ring;
    std::queue<RawAudioFrame> frames;

    AVCodecContext* codec = nullptr;

    int64_t first_timestamp_ns = -1;

    std::vector<float> sample_buffer;
    int input_channels = 0;
    int input_sample_rate = 0;
    int64_t total_samples_sent = 0;
};
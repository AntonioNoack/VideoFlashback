#pragma once

#include "PacketBuffer.hpp"
#include "EncoderWorker.hpp"

#include <thread>
#include <mutex>
#include <queue>
#include <condition_variable>


struct AVCodecContext;
struct SwsContext;


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

    bool initialize(int sampleRate, int channels);
    void push(RawAudioFrame frame);

    void set_audio_info();

private:

    void thread_main();
    void update_video_info();

private:

    PacketBuffer& ring;

    std::queue<RawAudioFrame> frames;

    AVCodecContext* codec = nullptr;
    SwsContext* scaler = nullptr;

    int64_t first_timestamp_ns = -1;

};
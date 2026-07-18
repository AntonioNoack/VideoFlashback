#pragma once

#include "RingBuffer.hpp"

#include <thread>
#include <mutex>
#include <queue>
#include <condition_variable>


struct AVCodecContext;
struct SwsContext;


struct RawFrame
{
    std::vector<uint8_t> data;

    int width;
    int height;

    int stride;
};


class Encoder
{
public:

    Encoder(
        RingBuffer& buffer);


    ~Encoder();


    bool initialize(
        int width,
        int height);


    void push(
        RawFrame frame);


private:

    void thread_main();


private:

    RingBuffer& ring;


    std::thread thread;

    std::mutex mutex;

    std::condition_variable condition;


    std::queue<RawFrame> frames;


    bool running = false;


    AVCodecContext* codec = nullptr;
    SwsContext* scaler = nullptr;

    int64_t frame_number = 0;
};
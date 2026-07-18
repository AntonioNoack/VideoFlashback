#pragma once

#include <deque>
#include <mutex>
#include <vector>
#include <cstdint>


struct EncodedPacket
{
    std::vector<uint8_t> data;

    int64_t pts = 0;
    int64_t dts = 0;

    bool keyframe = false;
};


class RingBuffer
{
public:

    explicit RingBuffer(
        int64_t duration_seconds);


    void push(
        EncodedPacket packet);


    std::vector<EncodedPacket> snapshot();


private:

    void trim();


private:

    std::mutex mutex;

    std::deque<EncodedPacket> packets;

    int64_t duration;

    int64_t newest_pts = 0;
};
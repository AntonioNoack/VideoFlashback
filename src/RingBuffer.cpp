#include "RingBuffer.hpp"

#include <iostream>

RingBuffer::RingBuffer(int64_t duration_seconds)
    : duration(duration_seconds)
{
}



void RingBuffer::push(
    EncodedPacket packet)
{
    std::lock_guard lock(mutex);

    if (false) std::cout
        << "Ring packet "
        << packet.data.size()
        << " bytes\n";

    newest_pts = packet.pts;
    packets.push_back(std::move(packet));

    trim();
}

void RingBuffer::trim()
{
    int64_t cutoff =
        newest_pts -
        duration * time_base;

    while (!packets.empty() &&
           packets.front().pts < cutoff)
    {
        packets.pop_front();
    }
}

std::vector<EncodedPacket>
RingBuffer::snapshot()
{
    std::lock_guard lock(mutex);


    auto start = packets.begin();
    while (start != packets.end() &&
        !start->keyframe)
    {
        ++start;
    }

    return {
        start,
        packets.end()
    };
}

void RingBuffer::set_video_info(
    const VideoInfo& info)
{
    std::lock_guard lock(mutex);

    video_info = info;
}


VideoInfo RingBuffer::get_video_info()
{
    std::lock_guard lock(mutex);

    return video_info;
}
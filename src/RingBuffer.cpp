#include "RingBuffer.hpp"


RingBuffer::RingBuffer(int64_t duration_seconds)
    : duration(duration_seconds)
{
}



void RingBuffer::push(
    EncodedPacket packet)
{
    std::lock_guard lock(mutex);


    newest_pts =
        packet.pts;


    packets.push_back(
        std::move(packet));


    trim();
}



void RingBuffer::trim()
{
    int64_t cutoff =
        newest_pts -
        duration * 90000; // 90k is the standard FFMPEG timebase according to Chatchy


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


    return {
        packets.begin(),
        packets.end()
    };
}
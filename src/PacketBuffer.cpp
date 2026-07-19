#include "PacketBuffer.hpp"

#include <algorithm>
#include <iostream>

PacketBuffer::PacketBuffer(int64_t duration_seconds)
    : duration(duration_seconds)
{
}



void PacketBuffer::push(
    EncodedPacket packet)
{
    std::lock_guard lock(mutex);

    if (false) std::cout
        << "Ring packet "
        << packet.data.size()
        << " bytes\n";

    // Keep the high-water mark; late video packets can arrive with older PTS.
    newest_pts = std::max(newest_pts, packet.pts);
    packets.push_back(std::move(packet));

    trim();
}

void PacketBuffer::trim()
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
PacketBuffer::snapshot()
{
    std::lock_guard lock(mutex);

    // Must start on a *video* keyframe. Audio packets are also marked as
    // keyframes (AAC), so treating any keyframe as a cut point leaves the
    // video track starting mid-GOP → black picture after the first save.
    auto start = packets.begin();
    while (start != packets.end() &&
           !(start->type == StreamType::Video && start->keyframe))
    {
        ++start;
    }

    if (start == packets.end())
        return {};

    const int64_t start_pts = start->pts;

    // Include A/V packets that belong at/after this keyframe's timeline,
    // even if a late-encoded older packet sits after it in the deque.
    std::vector<EncodedPacket> out;
    out.reserve(static_cast<size_t>(std::distance(start, packets.end())));
    for (auto it = start; it != packets.end(); ++it)
    {
        if (it->pts >= start_pts)
            out.push_back(*it);
    }

    return out;
}

void PacketBuffer::set_video_info(
    const VideoInfo& info)
{
    std::lock_guard lock(mutex);

    video_info = info;
}


VideoInfo PacketBuffer::get_video_info()
{
    std::lock_guard lock(mutex);

    return video_info;
}

void PacketBuffer::set_audio_info(
    const AudioInfo& info)
{
    std::lock_guard lock(mutex);

    audio_info = info;
}

AudioInfo PacketBuffer::get_audio_info()
{
    std::lock_guard lock(mutex);

    return audio_info;
}
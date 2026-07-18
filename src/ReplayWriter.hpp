#pragma once

#include "RingBuffer.hpp"

#include <string>


class ReplayWriter
{
public:

    bool write(
        const std::string& filename,
        RingBuffer& ring);


private:

    bool write_packets(
        void* format_context,
        const std::vector<EncodedPacket>& packets);
};
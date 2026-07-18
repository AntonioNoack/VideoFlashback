#pragma once

#include "PacketBuffer.hpp"

#include <string>


class ReplayWriter
{
public:

    bool write(
        const std::string& filename,
        PacketBuffer& ring);


private:

    bool write_packets(
        void* format_context,
        const std::vector<EncodedPacket>& packets);
};

#include <cstdint>
#include <vector>
#include <functional>

struct AudioFrame
{
    std::vector<float> samples;

    uint32_t frames;
    uint32_t channels;
    uint32_t sample_rate;
    uint64_t timestamp_ns;
};

using AudioFrameCallback =
    std::function<void(
        const float* samples,
        uint32_t frames,
        uint32_t channels,
        uint64_t timestamp)>;

class AudioCapture
{
public:
    bool initialize();
    bool connect_to_node(uint32_t node_id);
    void update();
    void shutdown();

    void set_callback(AudioFrameCallback callback);

private:
    static void on_stream_process(void*);

    AudioFrameCallback frame_callback;
    
};
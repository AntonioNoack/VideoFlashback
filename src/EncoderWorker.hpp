#pragma once

#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>

class EncoderWorker
{
public:

    EncoderWorker() = default;
    virtual ~EncoderWorker();

    void start();
    void stop();

protected:

    virtual void thread_main() = 0;

    std::thread worker;
    std::mutex mutex;
    std::condition_variable condition;
    std::atomic_bool running{false};
};
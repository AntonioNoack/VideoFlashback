#include "EncoderWorker.hpp"

EncoderWorker::~EncoderWorker()
{
    stop();
}

void EncoderWorker::start()
{
    if (running)
        return;

    running = true;

    worker =
        std::thread(
            &EncoderWorker::thread_main,
            this);
}

void EncoderWorker::stop()
{
    if (!running)
        return;

    running = false;

    condition.notify_all();

    if (worker.joinable())
        worker.join();
}
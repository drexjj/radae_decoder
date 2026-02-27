#include "audio_passthrough.h"

/* Use 8 kHz — matches the radio interface rate used by the RADE decoder,
   so the same device selection works for both modes. */
static constexpr unsigned int PASSTHROUGH_RATE   = 8000;
static constexpr int          PASSTHROUGH_FRAMES = 512;

bool AudioPassthrough::open(const std::string& input_hw_id,
                            const std::string& output_hw_id)
{
    close();

    rate_ = PASSTHROUGH_RATE;

    if (!stream_in_.open(input_hw_id, true, 1, rate_, PASSTHROUGH_FRAMES))
        return false;

    if (!stream_out_.open(output_hw_id, false, 1, rate_, PASSTHROUGH_FRAMES)) {
        stream_in_.close();
        return false;
    }

    return true;
}

void AudioPassthrough::close()
{
    stop();
    stream_in_.close();
    stream_out_.close();
    rate_ = 0;
}

void AudioPassthrough::start()
{
    if (!stream_in_.is_open() || !stream_out_.is_open() || running_) return;
    running_ = true;
    thread_  = std::thread(&AudioPassthrough::loop, this);
}

void AudioPassthrough::stop()
{
    running_ = false;
    if (thread_.joinable()) thread_.join();
}

void AudioPassthrough::loop()
{
    int16_t buf[PASSTHROUGH_FRAMES];

    /* Flush stale audio buffered before the stream started. */
    stream_in_.stop();

    while (running_.load(std::memory_order_relaxed)) {
        if (stream_in_.read(buf, PASSTHROUGH_FRAMES) != AUDIO_OK) continue;
        stream_out_.write(buf, PASSTHROUGH_FRAMES);
    }
}

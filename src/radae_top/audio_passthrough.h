#pragma once

#include <string>
#include <atomic>
#include <thread>
#include "../src/audio/audio_stream.h"

/* ── AudioPassthrough ──────────────────────────────────────────────────────
 *
 *  Copies raw audio from a capture stream directly to a playback stream
 *  with no processing — "analog" / monitor mode.
 *
 * ──────────────────────────────────────────────────────────────────────── */

class AudioPassthrough {
public:
    AudioPassthrough()  = default;
    ~AudioPassthrough() { stop(); close(); }

    AudioPassthrough(const AudioPassthrough&)            = delete;
    AudioPassthrough& operator=(const AudioPassthrough&) = delete;

    bool open(const std::string& input_hw_id,
              const std::string& output_hw_id);
    void close();
    void start();
    void stop();

    bool is_running() const { return running_.load(std::memory_order_relaxed); }

private:
    void loop();

    AudioStream        stream_in_;
    AudioStream        stream_out_;
    std::thread        thread_;
    std::atomic<bool>  running_{false};
    unsigned int       rate_ = 0;
};

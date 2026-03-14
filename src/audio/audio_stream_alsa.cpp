#include "audio_stream.h"

#include <alsa/asoundlib.h>
#include <string>
#include <vector>
#include <cstdio>

/* ── global init / terminate (ALSA needs no global init) ─────────────────── */

void audio_init()      {}
void audio_terminate() {}

/* ── device enumeration ──────────────────────────────────────────────────── */

static std::vector<AudioDevice> enumerate_alsa(bool capture)
{
    std::vector<AudioDevice> devices;
    fprintf(stderr, "Enumerating ALSA devices\n");

    int card = -1;
    while (snd_card_next(&card) == 0 && card >= 0) {
        snd_ctl_t* ctl = nullptr;
        char card_id[32];
        snprintf(card_id, sizeof(card_id), "hw:%d", card);

        if (snd_ctl_open(&ctl, card_id, 0) < 0)
            continue;

        snd_ctl_card_info_t* card_info = nullptr;
        snd_ctl_card_info_alloca(&card_info);
        const char* card_name = "";
        if (snd_ctl_card_info(ctl, card_info) == 0)
            card_name = snd_ctl_card_info_get_name(card_info);

        int dev = -1;
        while (snd_ctl_pcm_next_device(ctl, &dev) == 0 && dev >= 0) {
            snd_pcm_info_t* pcm_info = nullptr;
            snd_pcm_info_alloca(&pcm_info);
            snd_pcm_info_set_device(pcm_info, dev);
            snd_pcm_info_set_subdevice(pcm_info, 0);
            snd_pcm_info_set_stream(pcm_info,
                capture ? SND_PCM_STREAM_CAPTURE : SND_PCM_STREAM_PLAYBACK);

            if (snd_ctl_pcm_info(ctl, pcm_info) < 0)
                continue;  // device doesn't support this direction

            const char* dev_name = snd_pcm_info_get_name(pcm_info);

            AudioDevice ad;

            char hw_id[32];
            snprintf(hw_id, sizeof(hw_id), "plughw:%d,%d", card, dev);
            ad.hw_id = hw_id;

            char label[256];
            snprintf(label, sizeof(label), "plughw:%d,%d  (%s: %s)",
                     card, dev, card_name, dev_name ? dev_name : "");
            ad.name = label;

            devices.push_back(std::move(ad));
        }
        snd_ctl_close(ctl);
    }
    return devices;
}

std::vector<AudioDevice> audio_enumerate_capture_devices()
{
    return enumerate_alsa(true);
}

std::vector<AudioDevice> audio_enumerate_playback_devices()
{
    return enumerate_alsa(false);
}

/* ── AudioStream implementation ─────────────────────────────────────────── */

struct AudioStream::Impl {
    snd_pcm_t* pcm      = nullptr;
    int        channels = 1;
};

AudioStream::AudioStream()  = default;
AudioStream::~AudioStream() { close(); }

bool AudioStream::open(const std::string& device_id, bool is_input,
                       int channels, unsigned int sample_rate,
                       unsigned long frames_per_buffer)
{
    fprintf(stderr, "ALSA open\n");

    close();

    const char* dev = device_id.empty() ? "default" : device_id.c_str();
    snd_pcm_stream_t dir = is_input ? SND_PCM_STREAM_CAPTURE
                                    : SND_PCM_STREAM_PLAYBACK;

    snd_pcm_t* pcm = nullptr;
    if (snd_pcm_open(&pcm, dev, dir, 0) < 0) {
        fprintf(stderr, "ALSA: snd_pcm_open failed for '%s'\n", dev);
        return false;
    }

    /* Convert frames_per_buffer to microseconds for the latency hint */
    unsigned int latency_us = static_cast<unsigned int>(
        (unsigned long long)frames_per_buffer * 1000000ULL / sample_rate);

    int err = snd_pcm_set_params(pcm,
                                 SND_PCM_FORMAT_S16_LE,
                                 SND_PCM_ACCESS_RW_INTERLEAVED,
                                 static_cast<unsigned int>(channels),
                                 sample_rate,
                                 1,           /* allow software resampling */
                                 latency_us);
    if (err < 0) {
        fprintf(stderr, "ALSA: snd_pcm_set_params failed: %s\n",
                snd_strerror(err));
        snd_pcm_close(pcm);
        return false;
    }

    impl_           = new Impl;
    impl_->pcm      = pcm;
    impl_->channels = channels;
    return true;
}

void AudioStream::close()
{
    if (!impl_) return;
    if (impl_->pcm) {
        snd_pcm_drain(impl_->pcm);
        snd_pcm_close(impl_->pcm);
    }
    delete impl_;
    impl_ = nullptr;
}

void AudioStream::stop()
{
    if (impl_ && impl_->pcm)
        snd_pcm_drop(impl_->pcm);
}

void AudioStream::start()
{
    if (impl_ && impl_->pcm)
        snd_pcm_prepare(impl_->pcm);
}

void AudioStream::drain()
{
    if (impl_ && impl_->pcm)
        snd_pcm_drain(impl_->pcm);
}

AudioError AudioStream::read(void* buffer, unsigned long frames)
{
    if (!impl_ || !impl_->pcm) return AUDIO_ERROR;

    snd_pcm_sframes_t n = snd_pcm_readi(impl_->pcm, buffer, frames);
    if (n == -EPIPE) {
        /* overrun – recover and signal the caller */
        snd_pcm_prepare(impl_->pcm);
        return AUDIO_OVERFLOW;
    }
    if (n < 0) {
        snd_pcm_recover(impl_->pcm, static_cast<int>(n), 0);
        return AUDIO_ERROR;
    }
    return AUDIO_OK;
}

AudioError AudioStream::write(const void* buffer, unsigned long frames)
{
    if (!impl_ || !impl_->pcm) return AUDIO_ERROR;

    snd_pcm_sframes_t n = snd_pcm_writei(impl_->pcm, buffer, frames);
    if (n == -EPIPE) {
        /* underrun – recover */
        snd_pcm_prepare(impl_->pcm);
        return AUDIO_ERROR;
    }
    if (n < 0) {
        snd_pcm_recover(impl_->pcm, static_cast<int>(n), 0);
        return AUDIO_ERROR;
    }
    return AUDIO_OK;
}

#pragma once

#include <cstdint>

/* PCM format + DMA channel match for an SA-11x0 serial-audio path (transmit
   playback and receive/microphone capture). DDAR bit 0 is the only field
   separating transmit from receive (devman §11.6.1.1 RW), so a one-direction
   match MUST keep bit 0 in ddar_mask or it claims both channels. */
struct Sa11xxAudioConfig {
    uint32_t    ddar_mask;        /* DDAR bits the path matches on...          */
    uint32_t    ddar_value;       /* ...== this value selects the channel.     */
    uint16_t    channels;
    uint16_t    bits_per_sample;
    uint32_t    max_page_bytes;   /* per-DMA-buffer cap; oversized => declined. */
    bool        allow_resampler;
    const char* log_tag;          /* audio-thread name + log label.            */
};

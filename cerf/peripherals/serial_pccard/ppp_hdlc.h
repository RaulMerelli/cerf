#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

class PppHdlc {
public:
    using FrameFn = std::function<void(uint16_t protocol,
                                       const uint8_t* payload, size_t len)>;

    void SetFrameSink(FrameFn fn) { sink_ = std::move(fn); }

    void SetTxAccm(uint32_t accm) { tx_accm_ = accm; }

    /* Feed guest TX bytes; each complete, FCS-good frame is delivered to the
       sink. Frames failing the FCS check are silently discarded. */
    void Feed(const uint8_t* data, size_t n);

    /* Append one async-HDLC frame for (protocol, payload) to `out`: flag,
       Address/Control 0xFF 0x03, 2-byte protocol, payload, FCS-16 lo/hi, flag,
       with transparency stuffing applied. */
    void BuildFrame(uint16_t protocol, const uint8_t* payload, size_t len,
                    std::vector<uint8_t>& out) const;

private:
    void EndFrame();   /* closing flag arrived: verify FCS, parse, dispatch */

    FrameFn  sink_;
    uint32_t tx_accm_ = 0xFFFFFFFFu;

    std::vector<uint8_t> rx_;     /* de-stuffed bytes of the current frame */
    bool in_frame_  = false;
    bool in_escape_ = false;
};

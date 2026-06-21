#include "../../core/cerf_emulator.h"
#include "../../core/service.h"
#include "../../boards/board_detector.h"
#include "../../host/host_canvas.h"
#include "../../host/touch_input.h"
#include "../../socs/sa11xx/sa11xx_sp1_uart.h"
#include "../../state/emulation_freeze.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>

namespace {

/* Message IDs from Linux include/linux/mfd/ipaq-micro.h. */
constexpr uint8_t kSof          = 0x02;
constexpr uint8_t kMsgVersion   = 0x0;
constexpr uint8_t kMsgTouch     = 0x3;
constexpr uint8_t kMsgEepromRd  = 0x4;
constexpr uint8_t kMsgBattery   = 0x9;
constexpr uint8_t kMsgNvmRead   = 0xB;   /* SSC NVM read: payload [off_lo,off_hi,len] */

/* Sleeve NVM served for kMsgNvmRead. Byte layout MUST match ppcutil
   sub_1043C0's version-1 parse exactly (markers below) - any deviation fails
   the parse, so HH_dm never issues the IOCTL 0x0101010D subcmd-12 that sets
   OAL handshake bit 0xAC02034E.1, and PCMCIA card-detect never enables. */
constexpr uint8_t kSleeveNvm[] = {
    0xAA,                                /* @0   start ID */
    0x00, 0x00, 0x00, 0x00,              /* @1   header dword */
    0x01,                                /* @5   NVM version = 1 */
    0x00, 0x00,                          /* @6   field */
    0x00, 0x00,                          /* @8   field */
    'C', 'E', 'R', 'F', 0x00,            /* @10  name string (null-terminated) */
    0x00, 0x00, 0x00, 0x00, 0x00,        /* @15  5 bytes */
    0x00, 0x00, 0x00, 0x00,              /* @20  2x 16-bit fields */
    0x00, 0x00, 0x00, 0x00,              /* @24  2x 16-bit fields */
    0x0F, 0x0F, 0x0F, 0x0F,              /* @28  section terminator */
    0xBB,                                /* @32  start control */
    0x00, 0x00, 0x00, 0x00,              /* @33  4x 32-bit fields */
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x0F, 0x0F, 0x0F, 0x0F,              /* @49  terminator */
    0xFF, 0xFF, 0xFF, 0xFF,              /* @53  record list: empty */
    0x0F, 0x0F, 0x0F, 0x0F,              /* @57  section terminator */
    0x00, 0x00, 0x00, 0x00,              /* @61  blob1 count = 0 */
    0x0F, 0x0F, 0x0F, 0x0F,              /* @65  terminator */
    0x00, 0x00, 0x00, 0x00,              /* @69  blob2 count = 0 */
    0x0F, 0x0F, 0x0F, 0x0F,              /* @73  terminator */
    0x00, 0x00, 0x00, 0x00,              /* @77  blob3 count = 0 */
    0x0F, 0x0F, 0x0F, 0x0F,              /* @81  final terminator -> parse OK */
};

constexpr uint16_t kAdcXMin = 60;
constexpr uint16_t kAdcXMax = 960;
constexpr uint16_t kAdcYMin = 35;
constexpr uint16_t kAdcYMax = 980;
constexpr auto     kSamplePeriod = std::chrono::milliseconds(20);

class IpaqGen1MicroP : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardDetector>();
        return bd && bd->GetBoard() == Board::IpaqGen1;
    }

    void OnReady() override {
        emu_.Get<Sa11xxSp1Uart>().SetTxListener(
            [this](uint8_t b) { OnTxByte(b); });
    }

    /* Linux drivers/input/touchscreen/ipaq-micro-ts.c:
       press = MSG_TOUCHSCREEN with len=4, payload [Y_BE16, X_BE16].
       release = MSG_TOUCHSCREEN with len=0. */
    void SendTouchPress(uint16_t adc_x, uint16_t adc_y) {
        const uint8_t payload[4] = {
            static_cast<uint8_t>((adc_y >> 8) & 0xFFu),
            static_cast<uint8_t>(adc_y        & 0xFFu),
            static_cast<uint8_t>((adc_x >> 8) & 0xFFu),
            static_cast<uint8_t>(adc_x        & 0xFFu),
        };
        SendFrame(kMsgTouch, payload, 4);
    }
    void SendTouchRelease() { SendFrame(kMsgTouch, nullptr, 0); }

private:
    enum State : uint8_t { kWaitSof, kHaveSof, kData, kWaitCksum };

    std::mutex state_mtx_;
    State    state_         = kWaitSof;
    uint8_t  rx_id_         = 0;
    uint8_t  rx_len_        = 0;
    uint8_t  rx_count_      = 0;
    uint8_t  rx_buf_[16]    = {};

    void Push(uint8_t b) { emu_.Get<Sa11xxSp1Uart>().PushRxByte(b); }

    void SendFrame(uint8_t msg_id, const uint8_t* data, uint8_t len) {
        const uint8_t hdr = static_cast<uint8_t>(
            ((msg_id & 0x0Fu) << 4) | (len & 0x0Fu));
        uint8_t cksum = hdr;
        Push(kSof);
        Push(hdr);
        for (uint8_t i = 0; i < len; ++i) {
            Push(data[i]);
            cksum = static_cast<uint8_t>(cksum + data[i]);
        }
        Push(cksum);
    }

    void OnTxByte(uint8_t b) {
        std::lock_guard<std::mutex> lk(state_mtx_);
        switch (state_) {
            case kWaitSof:
                if (b == kSof) state_ = kHaveSof;
                break;
            case kHaveSof:
                rx_id_    = (b >> 4) & 0x0Fu;
                rx_len_   = b & 0x0Fu;
                rx_count_ = 0;
                state_    = (rx_len_ == 0) ? kWaitCksum : kData;
                break;
            case kData:
                if (rx_count_ < sizeof(rx_buf_)) rx_buf_[rx_count_] = b;
                ++rx_count_;
                if (rx_count_ >= rx_len_) state_ = kWaitCksum;
                break;
            case kWaitCksum:
                RespondTo(rx_id_, rx_buf_, rx_count_);
                state_ = kWaitSof;
                break;
        }
    }

    void RespondTo(uint8_t id, const uint8_t* req, uint8_t req_len) {
        switch (id) {
            case kMsgVersion: {
                const uint8_t v[4] = { '1', '.', '7', '7' };
                SendFrame(kMsgVersion, v, 4);
                break;
            }
            case kMsgEepromRd: {
                const uint8_t v[2] = { 0xFFu, 0xFFu };
                SendFrame(kMsgEepromRd, v, 2);
                break;
            }
            case kMsgBattery: {
                const uint8_t v[4] = { 0x01u, 0x00u, 0x0Fu, 0x01u };
                SendFrame(kMsgBattery, v, 4);
                break;
            }
            case kMsgNvmRead: {
                const uint16_t off = static_cast<uint16_t>(
                    (req_len > 0 ? req[0] : 0) |
                    ((req_len > 1 ? req[1] : 0) << 8));
                const uint8_t rlen = req_len > 2 ? req[2] : 0;
                uint8_t resp[16] = {};
                const uint8_t n = rlen > sizeof(resp)
                    ? static_cast<uint8_t>(sizeof(resp)) : rlen;
                for (uint8_t i = 0; i < n; ++i)
                    resp[i] = (off + i < sizeof(kSleeveNvm))
                        ? kSleeveNvm[off + i] : 0u;
                SendFrame(kMsgNvmRead, resp, n);
                break;
            }
            default:
                SendFrame(id, nullptr, 0);
                break;
        }
    }
};

static uint16_t MapAxis(int v, int v_max, uint16_t adc_lo, uint16_t adc_hi) {
    if (v <= 0)     return adc_lo;
    if (v >= v_max) return adc_hi;
    const uint32_t span = adc_hi - adc_lo;
    return static_cast<uint16_t>(
        adc_lo + (uint32_t)v * span / (uint32_t)v_max);
}

class IpaqGen1Touch : public TouchInput {
public:
    using TouchInput::TouchInput;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardDetector>();
        return bd && bd->GetBoard() == Board::IpaqGen1;
    }

    void OnReady() override {
        sampler_ = std::thread([this] { SamplerLoop(); });
    }

    ~IpaqGen1Touch() override {
        {
            std::lock_guard<std::mutex> lk(mtx_);
            stop_ = true;
        }
        cv_.notify_all();
        if (sampler_.joinable()) sampler_.join();
    }

    void OnPenDown(int x, int y) override {
        {
            std::lock_guard<std::mutex> lk(mtx_);
            last_x_   = x;
            last_y_   = y;
            pen_down_ = true;
        }
        cv_.notify_all();
    }
    void OnPenMove(int x, int y) override {
        std::lock_guard<std::mutex> lk(mtx_);
        if (!pen_down_) return;
        last_x_ = x;
        last_y_ = y;
    }
    void OnPenUp(int /*x*/, int /*y*/) override {
        {
            std::lock_guard<std::mutex> lk(mtx_);
            if (!pen_down_) return;
            pen_down_ = false;
        }
        cv_.notify_all();
    }
    void OnCaptureLost() override { OnPenUp(0, 0); }

private:
    std::mutex              mtx_;
    std::condition_variable cv_;
    std::thread             sampler_;
    bool                    stop_     = false;
    bool                    pen_down_ = false;
    int                     last_x_   = 0;
    int                     last_y_   = 0;

    void EmitPress(int x_host, int y_host) {
        auto&     hc = emu_.Get<HostCanvas>();
        const int w = (int)hc.GuestSurfaceWidth ();
        const int h = (int)hc.GuestSurfaceHeight();
        const uint16_t adc_x = MapAxis(x_host, w - 1, kAdcXMin, kAdcXMax);
        const uint16_t adc_y = MapAxis(y_host, h - 1, kAdcYMin, kAdcYMax);
        emu_.Get<IpaqGen1MicroP>().SendTouchPress(adc_x, adc_y);
    }

    void SamplerLoop() {
        auto& freeze = emu_.Get<EmulationFreeze>();
        std::unique_lock<std::mutex> lk(mtx_);
        bool was_down = false;
        while (!stop_) {
            if (pen_down_) {
                const int x = last_x_;
                const int y = last_y_;
                lk.unlock();
                {
                    auto frozen = freeze.WorkerSection();
                    EmitPress(x, y);
                }
                lk.lock();
                was_down = true;
                cv_.wait_for(lk, kSamplePeriod,
                             [&] { return stop_ || !pen_down_; });
            } else {
                if (was_down) {
                    was_down = false;
                    lk.unlock();
                    {
                        auto frozen = freeze.WorkerSection();
                        emu_.Get<IpaqGen1MicroP>().SendTouchRelease();
                    }
                    lk.lock();
                }
                cv_.wait(lk, [&] { return stop_ || pen_down_; });
            }
        }
        if (was_down) {
            lk.unlock();
            auto frozen = freeze.WorkerSection();
            emu_.Get<IpaqGen1MicroP>().SendTouchRelease();
        }
    }
};

}  /* namespace */

REGISTER_SERVICE(IpaqGen1MicroP);
REGISTER_SERVICE_AS(IpaqGen1Touch, TouchInput);

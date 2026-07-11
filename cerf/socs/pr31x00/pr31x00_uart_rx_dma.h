#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

class CerfEmulator;
class StateReader;
class StateWriter;

/* One UART's receive DMA channel (TMPR3911 §16.2.4, Fig 16.2.1). The engine has no view
   of the driver's read cursor and no way to stall the sender: it clocks received bytes
   into the buffer at the line rate and wraps under ENDMALOOP, overrunning unread bytes
   if the driver falls behind. */
class Pr31x00UartRxDma {
public:
    /* The engine's three interrupt outputs (Fig 16.2.1): a byte loaded into the Receive
       Holding Register, and the address counter reaching the mid point and the end of
       the buffer. */
    struct RxInts {
        bool rx        = false;
        bool dma_half  = false;
        bool dma_full  = false;
    };
    using RxIntFn    = std::function<void(const RxInts&)>;
    using LineIdleFn = std::function<void()>;

    Pr31x00UartRxDma(CerfEmulator& emu, const char* source);
    ~Pr31x00UartRxDma();

    /* raise_ints fires for each batch landed in the buffer, on_line_idle once the last
       received byte has been clocked out. Both run on the pacing thread. */
    void Start(RxIntFn raise_ints, LineIdleFn on_line_idle);
    void Stop();

    void SetBuffer(uint32_t pa);
    void SetLength(uint32_t bytes);
    void SetArmed(bool armed);
    void SetLineRate(uint32_t baud, double bits_per_char);

    uint32_t Count() const;
    uint32_t Length() const;
    bool     LineIdle() const;

    void Receive(const uint8_t* data, size_t n);

    void SaveState(StateWriter& w);
    void RestoreState(StateReader& r);

private:
    using Clock = std::chrono::steady_clock;

    void   PaceLoop();
    RxInts MeterLocked();

    CerfEmulator& emu_;
    const char*   source_;

    mutable std::mutex      mu_;
    std::condition_variable cv_;

    uint32_t buffer_pa_ = 0;
    uint32_t length_    = 0;
    uint32_t count_     = 0;
    bool     armed_     = false;

    uint32_t baud_          = 115200;
    double   bits_per_char_ = 10.0;

    std::vector<uint8_t> wire_;
    size_t               wire_pos_ = 0;
    double               credit_   = 0.0;
    Clock::time_point    last_;

    bool        stop_ = true;
    std::thread pacer_;
    RxIntFn     raise_ints_;
    LineIdleFn  on_line_idle_;
};

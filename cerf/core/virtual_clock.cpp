#include "virtual_clock.h"

#include "cerf_emulator.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

REGISTER_SERVICE(VirtualClock);

void VirtualClock::OnReady() {
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    host_hz_ = freq.QuadPart;
    epoch_ticks_.store(HostTicks(), std::memory_order_relaxed);
    accum_ticks_.store(0, std::memory_order_relaxed);
    running_.store(true, std::memory_order_relaxed);
}

int64_t VirtualClock::HostTicks() const {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return now.QuadPart;
}

int64_t VirtualClock::NowNs() const {
    for (;;) {
        const uint32_t s0 = seq_.load(std::memory_order_acquire);
        if ((s0 & 1u) != 0u) continue;

        const int64_t accum   = accum_ticks_.load(std::memory_order_relaxed);
        const int64_t epoch   = epoch_ticks_.load(std::memory_order_relaxed);
        const bool    running = running_.load(std::memory_order_relaxed);
        const int64_t host    = HostTicks();

        std::atomic_thread_fence(std::memory_order_acquire);
        if (seq_.load(std::memory_order_relaxed) != s0) continue;

        const int64_t ticks = running ? accum + (host - epoch) : accum;
        return static_cast<int64_t>(ScaleU64(static_cast<uint64_t>(ticks),
                                             1000000000ull,
                                             static_cast<uint64_t>(host_hz_)));
    }
}

void VirtualClock::Pause() {
    deficit_ticks_ = 0;
    if (!running_.load(std::memory_order_relaxed)) return;

    seq_.fetch_add(1, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);

    const int64_t host = HostTicks();
    accum_ticks_.store(accum_ticks_.load(std::memory_order_relaxed) +
                           (host - epoch_ticks_.load(std::memory_order_relaxed)),
                       std::memory_order_relaxed);
    running_.store(false, std::memory_order_relaxed);

    std::atomic_thread_fence(std::memory_order_release);
    seq_.fetch_add(1, std::memory_order_relaxed);
}

void VirtualClock::Resume() {
    if (running_.load(std::memory_order_relaxed)) return;

    seq_.fetch_add(1, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);

    epoch_ticks_.store(HostTicks(), std::memory_order_relaxed);
    running_.store(true, std::memory_order_relaxed);

    std::atomic_thread_fence(std::memory_order_release);
    seq_.fetch_add(1, std::memory_order_relaxed);
}

void VirtualClock::BeginIdleWait() {
    seq_.fetch_add(1, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);

    const int64_t host = HostTicks();
    accum_ticks_.store(accum_ticks_.load(std::memory_order_relaxed) +
                           (host - epoch_ticks_.load(std::memory_order_relaxed)),
                       std::memory_order_relaxed);
    running_.store(false, std::memory_order_relaxed);
    idle_wait_start_ticks_ = host;

    std::atomic_thread_fence(std::memory_order_release);
    seq_.fetch_add(1, std::memory_order_relaxed);
}

int64_t VirtualClock::EndIdleWait(int64_t cap_deadline_ns) {
    const int64_t host    = HostTicks();
    const int64_t elapsed = host - idle_wait_start_ticks_;
    const int64_t frozen  = accum_ticks_.load(std::memory_order_relaxed);

    const int64_t frozen_ns = static_cast<int64_t>(
        ScaleU64(static_cast<uint64_t>(frozen), 1000000000ull,
                 static_cast<uint64_t>(host_hz_)));
    int64_t repay = deficit_ticks_;
    if (repay > elapsed) repay = elapsed;
    const int64_t want = elapsed + repay;
    int64_t       fold = want;
    if (cap_deadline_ns != INT64_MAX) {
        int64_t cap_ns = cap_deadline_ns - frozen_ns;
        if (cap_ns < 0) cap_ns = 0;
        const int64_t cap_ticks = static_cast<int64_t>(
            ScaleU64(static_cast<uint64_t>(cap_ns),
                     static_cast<uint64_t>(host_hz_), 1000000000ull));
        if (cap_ticks < fold) fold = cap_ticks;
    }
    deficit_ticks_ = (deficit_ticks_ - repay) + (want - fold);
    if (deficit_ticks_ > host_hz_) deficit_ticks_ = host_hz_;

    seq_.fetch_add(1, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);

    accum_ticks_.store(frozen + fold, std::memory_order_relaxed);
    epoch_ticks_.store(host, std::memory_order_relaxed);
    running_.store(true, std::memory_order_relaxed);

    std::atomic_thread_fence(std::memory_order_release);
    seq_.fetch_add(1, std::memory_order_relaxed);

    return static_cast<int64_t>(
        ScaleU64(static_cast<uint64_t>(fold), 1000000000ull,
                 static_cast<uint64_t>(host_hz_)));
}

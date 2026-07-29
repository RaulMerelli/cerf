#include "virtual_timer_list.h"

#include "cerf_emulator.h"
#include "log.h"
#include "virtual_clock.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <intrin.h>

REGISTER_SERVICE(VirtualTimerList);

void VirtualTimerList::OnReady() {
    LARGE_INTEGER freq, q0, q1;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&q0);
    const uint64_t t0 = __rdtsc();
    do {
        QueryPerformanceCounter(&q1);
    } while ((q1.QuadPart - q0.QuadPart) * 1000 < freq.QuadPart);
    const uint64_t t1 = __rdtsc();
    tsc_per_sec_ = VirtualClock::ScaleU64(
        t1 - t0, static_cast<uint64_t>(freq.QuadPart),
        static_cast<uint64_t>(q1.QuadPart - q0.QuadPart));
    tsc_hz_boot_ = tsc_per_sec_;
    qpf_     = static_cast<uint64_t>(freq.QuadPart);
    cal_tsc_ = t1;
    cal_qpc_ = q1.QuadPart;
    LOG(Boot, "VirtualTimerList: TSC %llu ticks/sec\n",
        static_cast<unsigned long long>(tsc_per_sec_));
}

void VirtualTimerList::Recalibrate(uint64_t tsc) {
    if (tsc <= cal_tsc_) {
        LARGE_INTEGER q;
        QueryPerformanceCounter(&q);
        cal_tsc_ = tsc;
        cal_qpc_ = q.QuadPart;
        return;
    }
    if (tsc - cal_tsc_ < tsc_per_sec_ / 10) return;
    LARGE_INTEGER q;
    QueryPerformanceCounter(&q);
    const int64_t dqpc = q.QuadPart - cal_qpc_;
    if (dqpc <= 0) {
        cal_tsc_ = tsc;
        cal_qpc_ = q.QuadPart;
        return;
    }
    const uint64_t cand = VirtualClock::ScaleU64(
        tsc - cal_tsc_, qpf_, static_cast<uint64_t>(dqpc));
    if (cand >= tsc_hz_boot_ / 2 && cand <= tsc_hz_boot_ * 2) {
        tsc_per_sec_ = cand;
    }
    cal_tsc_ = tsc;
    cal_qpc_ = q.QuadPart;
}

VirtualTimerList::Entry* VirtualTimerList::Add(std::function<void()> fn) {
    entries_.push_back(std::make_unique<Entry>(std::move(fn)));
    entries_.back()->owner_ = this;
    return entries_.back().get();
}

void VirtualTimerList::RecomputeNextTsc() {
    const int64_t earliest = NextDeadlineNs();
    if (earliest == kNoDeadline) {
        next_deadline_tsc_ = UINT64_MAX;
        return;
    }
    const int64_t now = emu_.Get<VirtualClock>().NowNs();
    if (earliest <= now) {
        next_deadline_tsc_ = 0;
        return;
    }
    const uint64_t tsc = __rdtsc();
    Recalibrate(tsc);
    next_deadline_tsc_ =
        tsc + VirtualClock::ScaleU64(
                  static_cast<uint64_t>(earliest - now), tsc_per_sec_,
                  1000000000ull);
}

int64_t VirtualTimerList::NextDeadlineNs() const {
    int64_t earliest = kNoDeadline;
    for (const auto& e : entries_) {
        if (e->deadline_ns_ < earliest) earliest = e->deadline_ns_;
    }
    return earliest;
}

void VirtualTimerList::RunExpired(Site site) {
    const int64_t earliest = NextDeadlineNs();
    if (earliest == kNoDeadline) return;

    if (site == Site::RunLoop && __rdtsc() < next_deadline_tsc_) return;

    const int64_t now = emu_.Get<VirtualClock>().NowNs();

    if (earliest > now) {
        RecomputeNextTsc();
        return;
    }

    for (size_t i = 0; i < entries_.size(); ++i) {
        Entry* e = entries_[i].get();
        if (e->deadline_ns_ > now) continue;
        e->deadline_ns_ = kNoDeadline;
        e->fn_();
    }
    RecomputeNextTsc();
}

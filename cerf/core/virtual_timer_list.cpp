#include "virtual_timer_list.h"

#include "cerf_emulator.h"
#include "fatal.h"
#include "host_thread_priority.h"
#include "log.h"
#include "virtual_clock.h"
#include "../state/emulation_freeze.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

REGISTER_SERVICE(VirtualTimerList);

namespace {
constexpr int64_t kSpinHorizonNs = 10200000;
}

void VirtualTimerList::OnReady() {
    timer_ = CreateWaitableTimerExW(nullptr, nullptr,
                                    CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                    TIMER_ALL_ACCESS);
    if (timer_ == nullptr) timer_ = CreateWaitableTimerW(nullptr, FALSE, nullptr);
    wake_event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (timer_ == nullptr || wake_event_ == nullptr) {
        emu_.Get<Fatal>().Die(
            "VirtualTimerList: timer/event creation failed gle=%lu",
            GetLastError());
    }
    /* util/qemu-timer.c qemu_clock_enable: the enable edge notifies the
       clock's timer lists. */
    emu_.Get<VirtualClock>().RegisterEnableNotify(
        [this] { SetEvent(wake_event_); });
    thread_ = std::thread([this] { ExpiryLoop(); });
}

void VirtualTimerList::OnShutdown() {
    stop_.store(true, std::memory_order_release);
    if (wake_event_ != nullptr) SetEvent(wake_event_);
    if (thread_.joinable()) thread_.join();
    if (timer_ != nullptr)      { CloseHandle(timer_);      timer_ = nullptr; }
    if (wake_event_ != nullptr) { CloseHandle(wake_event_); wake_event_ = nullptr; }
}

VirtualTimerList::~VirtualTimerList() { OnShutdown(); }

VirtualTimerList::Entry* VirtualTimerList::Add(std::function<void()> fn) {
    std::lock_guard<std::mutex> lk(mtx_);
    entries_.push_back(std::make_unique<Entry>(std::move(fn)));
    entries_.back()->owner_ = this;
    return entries_.back().get();
}

int64_t VirtualTimerList::Entry::DeadlineNs() const {
    std::lock_guard<std::mutex> lk(owner_->mtx_);
    return deadline_ns_;
}

void VirtualTimerList::ArmEntry(Entry* e, int64_t deadline_ns) {
    bool wake;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        e->deadline_ns_ = deadline_ns;
        /* util/qemu-timer.c timer_mod_ns: the list is notified only when the
           armed timer becomes the head of the active list. */
        wake = deadline_ns < waiting_until_;
    }
    if (wake) {
        wake_gen_.fetch_add(1, std::memory_order_release);
        SetEvent(wake_event_);
    }
}

int64_t VirtualTimerList::NextDeadlineLocked() const {
    int64_t earliest = kNoDeadline;
    for (const auto& e : entries_) {
        if (e->deadline_ns_ < earliest) earliest = e->deadline_ns_;
    }
    return earliest;
}

/* util/qemu-timer.c timerlist_run_timers. */
void VirtualTimerList::RunExpired() {
    auto& clock = emu_.Get<VirtualClock>();
    /* util/qemu-timer.c timerlist_run_timers: a disabled clock runs no
       timers. */
    if (!clock.Running()) {
        return;
    }
    const int64_t now = clock.NowNs();
    for (;;) {
        std::function<void()>* fn = nullptr;
        {
            std::lock_guard<std::mutex> lk(mtx_);
            Entry* best = nullptr;
            for (const auto& e : entries_) {
                if (e->deadline_ns_ > now) continue;
                if (best == nullptr || e->deadline_ns_ < best->deadline_ns_) {
                    best = e.get();
                }
            }
            if (best != nullptr) {
                best->deadline_ns_ = kNoDeadline;
                fn = &best->fn_;
            }
        }
        if (fn == nullptr) return;
        (*fn)();
    }
}

void VirtualTimerList::ExpiryLoop() {
    emu_.Get<HostThreadPriority>().Elevate(HostThreadRole::TimerExpiry);
    auto& freeze = emu_.Get<EmulationFreeze>();
    auto& clock  = emu_.Get<VirtualClock>();
    while (!stop_.load(std::memory_order_acquire)) {
        {
            auto frozen = freeze.WorkerSection();
            RunExpired();
        }

        std::unique_lock<std::mutex> lk(mtx_);
        if (stop_.load(std::memory_order_acquire)) break;
        const int64_t next = NextDeadlineLocked();
        /* util/qemu-timer.c timerlist_deadline_ns: no active timers, or a
           disabled clock, means no deadline. */
        const bool enabled = clock.Running();
        if (next != kNoDeadline && enabled &&
            next - clock.NowNs() <= 0) {
            continue;
        }
        waiting_until_ = next;
        const uint32_t wait_gen = wake_gen_.load(std::memory_order_acquire);
        lk.unlock();

        if (next == kNoDeadline || !enabled) {
            WaitForSingleObject(wake_event_, INFINITE);
        } else {
            /* dolphin-emu Source/Core/Common/Timer.cpp
               PrecisionTimer::SleepUntil. */
            bool woken = false;
            for (;;) {
                if (!clock.Running()) break;
                const int64_t sleep_ns = next - clock.NowNs() - kSpinHorizonNs;
                if (sleep_ns <= 0) break;
                LARGE_INTEGER due;
                /* util/qemu-timer.c qemu_timeout_ns_to_ms: "Always round
                   up". */
                due.QuadPart = -((sleep_ns + 99) / 100);
                SetWaitableTimer(timer_, &due, 0, nullptr, nullptr, FALSE);
                HANDLE objs[2] = { timer_, wake_event_ };
                const DWORD wr =
                    WaitForMultipleObjects(2, objs, FALSE, INFINITE);
                CancelWaitableTimer(timer_);
                if (wr == WAIT_OBJECT_0 + 1) {
                    woken = true;
                    break;
                }
            }
            if (!woken) {
                while (clock.Running() && clock.NowNs() < next) {
                    if (wake_gen_.load(std::memory_order_acquire) !=
                        wait_gen) {
                        break;
                    }
                    YieldProcessor();
                }
            }
        }

        lk.lock();
        waiting_until_ = 0;
    }
}

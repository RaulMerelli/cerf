#pragma once

#include "../core/service.h"

#define NOMINMAX
#include <windows.h>

#include <cstdint>
#include <functional>
#include <string>
#include <thread>

class StateWriter;
class StateReader;

/* Callers must run Save/Restore on a thread other than the JIT thread -
   JitRunner::Pause self-deadlocks if called from the JIT thread. */
class Hibernation : public Service {
public:
    using Service::Service;
    ~Hibernation() override;

    void OnReady() override;

    /* state.img in the device directory - the implicit save/restore target. */
    std::wstring DefaultStatePath() const;
    bool         DefaultStateExists() const;

    /* Signaled when the most recent SaveAsync/RestoreAsync worker finishes. */
    HANDLE DoneEvent() const { return done_event_; }

    /* Empty path → DefaultStatePath. ram_only applies only the RAM
       section (warm boot): cold-entry CPU/cp15/peripherals stay intact
       for the kernel to re-init. */
    bool Save(const std::wstring& path);
    bool Restore(const std::wstring& path, bool ram_only = false,
                 bool cold_boot_on_failure = false);

    /* Runs the op on a worker. on_done (if set) fires on that worker thread
       at completion - UI work inside it must marshal to the UI thread.
       Serialized: a new call joins the previous worker first. */
    void SaveAsync(const std::wstring& path, std::function<void()> on_done = {});
    void RestoreAsync(const std::wstring& path, bool ram_only = false);

    void OnShutdown() override;

private:
    void     WriteHeader(StateWriter& w) const;
    bool     ValidateHeader(StateReader& r);
    void     RestorePeripherals(StateReader& r);
    void     RestorePresentation(StateReader& r);
    uint32_t PeripheralLayoutSig() const;
    void     Progress(const char* fmt, ...);
    void     AwaitFailureAck(bool cold_boot);
    void     JoinWorker();

    std::thread worker_;
    HANDLE      done_event_ = nullptr;
};

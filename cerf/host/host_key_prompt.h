#pragma once

#include "../core/service.h"

#define NOMINMAX
#include <windows.h>

#include <atomic>
#include <cstdint>

/* The UART-screen host-key channel. A waiter (the saved-state boot prompt, a
   restore-failure hold) arms it, calls Wait until a key arrives or it times
   out, then disarms. HostInputCapture's low-level keyboard hook feeds OnKey
   while a waiter is armed. There is never more than one waiter at a time. */
class HostKeyPrompt : public Service {
public:
    using Service::Service;
    void OnReady() override;
    ~HostKeyPrompt() override;

    /* Fed by the UI-thread LL key hook on key-down while armed. */
    void OnKey(uint32_t vk);
    bool Armed() const { return armed_.load(std::memory_order_acquire); }

    void     Arm();
    uint32_t Wait(DWORD timeout_ms);   /* virtual-key code, or 0 on timeout */
    void     Disarm();

private:
    std::atomic<bool>     armed_{false};
    std::atomic<uint32_t> key_{0};
    HANDLE                key_event_ = nullptr;
};

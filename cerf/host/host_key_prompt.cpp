#include "host_key_prompt.h"

#include "../core/cerf_emulator.h"
#include "../core/log.h"

REGISTER_SERVICE(HostKeyPrompt);

void HostKeyPrompt::OnReady() {
    key_event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);  /* auto-reset */
    if (!key_event_) {
        LOG(Caution, "HostKeyPrompt: CreateEvent failed gle=%lu\n", GetLastError());
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
}

HostKeyPrompt::~HostKeyPrompt() {
    if (key_event_) CloseHandle(key_event_);
}

void HostKeyPrompt::OnKey(uint32_t vk) {
    if (!armed_.load(std::memory_order_acquire)) return;
    key_.store(vk, std::memory_order_release);
    SetEvent(key_event_);
}

void HostKeyPrompt::Arm() {
    key_.store(0, std::memory_order_release);
    ResetEvent(key_event_);
    armed_.store(true, std::memory_order_release);
}

uint32_t HostKeyPrompt::Wait(DWORD timeout_ms) {
    if (WaitForSingleObject(key_event_, timeout_ms) == WAIT_OBJECT_0)
        return key_.exchange(0, std::memory_order_acq_rel);
    return 0;
}

void HostKeyPrompt::Disarm() {
    armed_.store(false, std::memory_order_release);
}

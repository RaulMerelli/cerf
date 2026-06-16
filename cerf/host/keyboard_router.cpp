#include "keyboard_router.h"

#include "../core/cerf_emulator.h"
#include "keyboard_input.h"

REGISTER_SERVICE(KeyboardRouter);

void KeyboardRouter::Register(KeyboardInput* src) {
    std::lock_guard<std::mutex> lk(mtx_);
    sources_.push_back(src);
    if (!active_ || src->SourcePriority() > active_->SourcePriority())
        active_ = src;
}

void KeyboardRouter::OnHostKey(uint8_t vk, bool key_up) {
    KeyboardInput* a;
    { std::lock_guard<std::mutex> lk(mtx_); a = active_; }
    if (a) a->OnHostKey(vk, key_up);
}

std::vector<KeyboardInput*> KeyboardRouter::Sources() {
    std::lock_guard<std::mutex> lk(mtx_);
    return sources_;
}

KeyboardInput* KeyboardRouter::Active() {
    std::lock_guard<std::mutex> lk(mtx_);
    return active_;
}

void KeyboardRouter::SetActive(KeyboardInput* src) {
    std::lock_guard<std::mutex> lk(mtx_);
    for (auto* s : sources_)
        if (s == src) { active_ = src; return; }
}

void KeyboardRouter::SetActiveByName(const std::wstring& name) {
    std::lock_guard<std::mutex> lk(mtx_);
    for (auto* s : sources_)
        if (s->SourceName() == name) { active_ = s; return; }
}

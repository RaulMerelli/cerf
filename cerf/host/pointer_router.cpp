#include "pointer_router.h"

#include "../core/cerf_emulator.h"
#include "pointer_input.h"
#include "pointer_source.h"
#include "relative_mouse_input.h"
#include "touch_input.h"

REGISTER_SERVICE(PointerRouter);

void PointerRouter::OnReady() {
    /* One winner per base today (REGISTER_SERVICE_AS); a future second source of
       the same kind self-registers from its own OnReady (Register dedups). */
    if (auto* p = emu_.TryGet<PointerInput>())       Register(p);
    if (auto* r = emu_.TryGet<RelativeMouseInput>()) Register(r);
    if (auto* t = emu_.TryGet<TouchInput>())         Register(t);
}

void PointerRouter::Register(PointerSource* src) {
    std::lock_guard<std::mutex> lk(mtx_);
    for (auto* s : sources_) if (s == src) return;
    sources_.push_back(src);
    if (!active_ || src->SourcePriority() > active_->SourcePriority())
        active_ = src;
}

std::vector<PointerSource*> PointerRouter::Sources() {
    std::lock_guard<std::mutex> lk(mtx_);
    return sources_;
}

PointerSource* PointerRouter::Active() {
    std::lock_guard<std::mutex> lk(mtx_);
    return active_;
}

void PointerRouter::SetActive(PointerSource* src) {
    std::lock_guard<std::mutex> lk(mtx_);
    for (auto* s : sources_)
        if (s == src) { active_ = src; return; }
}

void PointerRouter::SetActiveByName(const std::wstring& name) {
    std::lock_guard<std::mutex> lk(mtx_);
    for (auto* s : sources_)
        if (s->SourceName() == name) { active_ = s; return; }
}

void PointerRouter::CycleNext() {
    std::lock_guard<std::mutex> lk(mtx_);
    if (sources_.size() < 2) return;
    for (size_t i = 0; i < sources_.size(); ++i) {
        if (sources_[i] == active_) {
            active_ = sources_[(i + 1) % sources_.size()];
            return;
        }
    }
    active_ = sources_.front();
}

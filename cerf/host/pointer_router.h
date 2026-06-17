#pragma once

#include "../core/service.h"

#include <mutex>
#include <string>
#include <vector>

class PointerSource;

/* Registry + active-selection funnel for pointing-device sources (mirror of
   KeyboardRouter). Sources are discovered in OnReady; HostCanvasInput reads
   Active()->Kind() to route host mouse messages, and PointerWidget reads
   Sources()/Active() and drives SetActive / CycleNext. */
class PointerRouter : public Service {
public:
    using Service::Service;

    void OnReady() override;

    void Register(PointerSource* src);

    std::vector<PointerSource*> Sources();
    PointerSource*              Active();
    void                        SetActive(PointerSource* src);
    void                        SetActiveByName(const std::wstring& name);
    void                        CycleNext();

private:
    std::mutex                  mtx_;
    std::vector<PointerSource*> sources_;
    PointerSource*              active_ = nullptr;
};

#pragma once

#include "../core/service.h"

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

class KeyboardInput;

/* Registry + active-selection funnel for keyboard sources. Sources self-register
   from OnReady; host-key consumers call OnHostKey, which forwards to the active
   source. The keyboard widget reads Sources()/Active() and drives SetActive. */
class KeyboardRouter : public Service {
public:
    using Service::Service;

    void Register(KeyboardInput* src);
    void OnHostKey(uint8_t vk, bool key_up);

    std::vector<KeyboardInput*> Sources();
    KeyboardInput*              Active();
    void                        SetActive(KeyboardInput* src);
    void                        SetActiveByName(const std::wstring& name);

private:
    std::mutex                  mtx_;
    std::vector<KeyboardInput*> sources_;
    KeyboardInput*              active_ = nullptr;
};

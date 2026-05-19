#pragma once

#include "../core/service.h"

class TouchInput : public Service {
public:
    using Service::Service;
    ~TouchInput() override = default;

    virtual void OnPenDown    (int x, int y) = 0;
    virtual void OnPenMove    (int x, int y) = 0;
    virtual void OnPenUp      (int x, int y) = 0;
    virtual void OnCaptureLost()             = 0;
};

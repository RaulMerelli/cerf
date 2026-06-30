#pragma once

#include "service.h"

/* Registers only when board_id is missing/unrecognised (BoardContext == Unknown).
   EnsureFound() reports + exits; invoked by RuntimeUserErrorsService after the
   device-present check, so a non-existent device is reported as such first. */
class BoardNotFoundService : public Service {
public:
    using Service::Service;
    bool ShouldRegister() override;
    void EnsureFound();
};

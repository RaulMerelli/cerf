#pragma once

#include "../../core/service.h"

#include <cstdint>

/* Per-page guest-memory blob copy through the live MMU, in the issuing
   thread's process context. */
class CerfVirtGuestMem : public Service {
public:
    using Service::Service;

    bool ReadBlob(uint32_t va, void* dst, uint32_t n);
    bool WriteBlob(uint32_t va, const void* src, uint32_t n);
};

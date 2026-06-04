#pragma once

#include "../../core/service.h"

#include <cstdint>

/* Per-page guest-memory blob copy through the live MMU, in the issuing thread's
   process context. Shared by the FolderSharing peripheral (the ServerPB) and
   the file-ops service (the fDTAPtr read/write buffer). */
class FolderShareMmu : public Service {
public:
    using Service::Service;

    bool ReadBlob(uint32_t va, void* dst, uint32_t n);
    bool WriteBlob(uint32_t va, const void* src, uint32_t n);
};

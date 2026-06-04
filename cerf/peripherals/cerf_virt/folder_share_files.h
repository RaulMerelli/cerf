#pragma once

#define NOMINMAX
#include <windows.h>

#include "../../core/service.h"
#include "cerf_virt_folder_share_regs.h"

#include <cstdint>
#include <string>

/* Open-file lifecycle + file I/O half of the folder-share server. Owns the
   guest-handle -> Win32 HANDLE table; Read/Write move bytes between the host
   file and the guest's fDTAPtr buffer through the live MMU. */
class FolderShareFiles : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;

    static bool Owns(uint32_t code);
    /* Returns the value for the channel Result register: a kError* code for
       most ops, or the max I/O size for kServerGetMaxIOSize. */
    uint32_t Run(uint32_t code, CerfVirt::ServerPB& pb);

private:
    static constexpr int kMaxFd = 40;   /* matches the guest FSD's gFDList[] */
    struct Slot {
        HANDLE       h = INVALID_HANDLE_VALUE;
        uint16_t     open_mode = 0;
        std::wstring name;
    };
    Slot files_[kMaxFd];

    uint16_t AllocSlot();

    uint16_t GetDriveConfig(CerfVirt::ServerPB& pb);
    uint16_t Create(CerfVirt::ServerPB& pb);
    uint16_t Open(CerfVirt::ServerPB& pb);
    uint16_t Read(CerfVirt::ServerPB& pb);
    uint16_t Write(CerfVirt::ServerPB& pb);
    uint16_t SetEof(CerfVirt::ServerPB& pb);
    uint16_t Close(CerfVirt::ServerPB& pb);
    uint16_t GetFcbInfo(CerfVirt::ServerPB& pb);
    uint16_t GetSpace(CerfVirt::ServerPB& pb);
};

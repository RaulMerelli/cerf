#pragma once

#define NOMINMAX
#include <windows.h>

#include "../../core/service.h"
#include "cerf_virt_folder_share_regs.h"

#include <cstdint>

/* Directory / name / enumeration half of the folder-share server: MkDir, RmDir,
   Delete, Rename, SetAttributes, and GetInfo (existence check + FindFirst/Next).
   Owns the transaction-id -> Win32 find-handle table. */
class FolderShareDir : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    static bool Owns(uint32_t code);
    uint32_t Run(uint32_t code, CerfVirt::ServerPB& pb);

private:
    static constexpr int kMaxFc = 40;   /* matches the guest FSD's gFCList[] */
    HANDLE finds_[kMaxFc];

    uint16_t MkDir(CerfVirt::ServerPB& pb);
    uint16_t RmDir(CerfVirt::ServerPB& pb);
    uint16_t Delete(CerfVirt::ServerPB& pb);
    uint16_t Rename(CerfVirt::ServerPB& pb);
    uint16_t SetAttributes(CerfVirt::ServerPB& pb);
    uint16_t GetInfo(CerfVirt::ServerPB& pb);

    static void UpdateFromFind(CerfVirt::ServerPB& pb,
                               const WIN32_FIND_DATAW& fd, bool writeback_name);
};

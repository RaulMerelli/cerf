#pragma once

#define NOMINMAX
#include <windows.h>

#include "../../core/service.h"
#include "cerf_virt_folder_share_regs.h"

#include <cstdint>

class FolderShareDir : public Service {
public:
    explicit FolderShareDir(CerfEmulator& emu) : Service(emu) {
        for (int i = 0; i < kMaxFc; ++i) finds_[i] = INVALID_HANDLE_VALUE;
    }
    ~FolderShareDir() override;

    bool ShouldRegister() override;
    void OnReady() override;

    static bool Owns(uint32_t code);
    uint32_t Run(uint32_t code, CerfVirt::ServerPB& pb);

    void CloseAll();
    void ReconcileGeneration();

private:
    static constexpr int kMaxFc = 40;
    HANDLE   finds_[kMaxFc];
    uint32_t config_generation_ = 0;

    uint16_t MkDir(CerfVirt::ServerPB& pb);
    uint16_t RmDir(CerfVirt::ServerPB& pb);
    uint16_t Delete(CerfVirt::ServerPB& pb);
    uint16_t Rename(CerfVirt::ServerPB& pb);
    uint16_t SetAttributes(CerfVirt::ServerPB& pb);
    uint16_t GetInfo(CerfVirt::ServerPB& pb);
    uint16_t CloseFind(CerfVirt::ServerPB& pb);
};

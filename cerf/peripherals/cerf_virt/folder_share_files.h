#pragma once

#define NOMINMAX
#include <windows.h>

#include "../../core/service.h"
#include "cerf_virt_folder_share_regs.h"

#include <cstdint>
#include <string>

class StateWriter;
class StateReader;

class FolderShareFiles : public Service {
public:
    using Service::Service;
    ~FolderShareFiles() override;

    bool ShouldRegister() override;
    void OnReady() override;

    static bool Owns(uint32_t code);

    uint32_t Run(uint32_t code, CerfVirt::ServerPB& pb);

    void CloseAll();
    void ReconcileGeneration();

    void SaveState(StateWriter& w);
    void RestoreState(StateReader& r);

private:
    static constexpr int kMaxFd = 40;
    struct Slot {
        HANDLE       h   = INVALID_HANDLE_VALUE;
        uint16_t     seq = 0;
        std::wstring name;
    };
    Slot     files_[kMaxFd];
    uint16_t open_seq_          = 0;
    uint32_t config_generation_ = 0;

    Slot*    SlotFor(uint16_t handle);
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

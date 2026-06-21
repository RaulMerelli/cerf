#include "cerf_virt_addr_map.h"
#include "cerf_virt_folder_share_regs.h"
#include "cerf_virt_guest_mem.h"
#include "folder_share_files.h"
#include "folder_share_dir.h"

#include "../peripheral_base.h"
#include "../peripheral_dispatcher.h"
#include "../../core/cerf_emulator.h"
#include "../../core/device_config.h"
#include "../../core/folder_share_config.h"
#include "../../core/log.h"
#include "../../state/state_stream.h"

#include <cstring>

using namespace CerfVirt;

namespace {

constexpr uint32_t kMountBytes = kFsMountPointMaxWchars * sizeof(uint16_t);

/* The op MUST run synchronously in the issuing (JIT) thread: the ServerPB is
   read by guest VA through the live MMU, valid only in that thread's process
   context - a worker thread would translate the VA against the wrong process. */
class CerfVirtFolderShare : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        return emu_.Get<DeviceConfig>().guest_additions;
    }

    void OnReady() override {
        emu_.Get<PeripheralDispatcher>().Register(this);
        std::memset(mount_bytes_, 0, sizeof(mount_bytes_));
    }

    uint32_t MmioBase() const override { return CerfVirt::kFolderShareBase; }
    uint32_t MmioSize() const override { return CerfVirt::kFolderShareSize; }

    uint32_t ReadWord(uint32_t addr) override {
        const uint32_t off = addr - MmioBase();
        switch (off) {
            case kFsServerPbAddr: return serverpb_va_;
            case kFsCode:         return last_code_;
            case kFsIoPending:    return 0u;          /* synchronous: never pending */
            case kFsResult:       return result_;
            case kFsEnabled:      return emu_.Get<FolderShareConfig>().Enabled() ? 1u : 0u;
            case kFsGeneration:   return emu_.Get<FolderShareConfig>().Generation();
            default: break;
        }
        if (off >= kFsMountPoint && off + 4u <= kFsMountPoint + kMountBytes) {
            RefreshMount();
            uint32_t v;
            std::memcpy(&v, mount_bytes_ + (off - kFsMountPoint), 4);
            return v;
        }
        return 0u;
    }

    uint8_t ReadByte(uint32_t addr) override {
        const uint32_t w = ReadWord(addr & ~3u);
        return (uint8_t)(w >> ((addr & 3u) * 8u));
    }
    uint16_t ReadHalf(uint32_t addr) override {
        const uint32_t w = ReadWord(addr & ~3u);
        return (uint16_t)(w >> ((addr & 2u) * 8u));
    }

    void WriteWord(uint32_t addr, uint32_t value) override {
        const uint32_t off = addr - MmioBase();
        switch (off) {
            case kFsServerPbAddr: serverpb_va_ = value; break;
            case kFsCode:         last_code_ = value; HandleCode(value); break;
            case kFsResult:       result_ = value; break;
            default: break;   /* Enabled/Generation/MountPoint are read-only */
        }
    }

    /* serverpb_va_ is guest-registered and not re-sent on a resume, so a
       mounted share breaks without it. mount_* is a lazy cache of
       FolderShareConfig (host config, persists across restart) - left
       uninited so the next read rebuilds it from the live config. */
    void SaveState(StateWriter& w) override {
        w.Write(serverpb_va_);
        w.Write(last_code_);
        w.Write(result_);
    }
    void RestoreState(StateReader& r) override {
        r.Read(serverpb_va_);
        r.Read(last_code_);
        r.Read(result_);
    }

private:
    void RefreshMount() {
        auto& cfg = emu_.Get<FolderShareConfig>();
        const uint32_t g = cfg.Generation();
        if (mount_inited_ && g == mount_gen_) return;
        mount_gen_ = g;
        mount_inited_ = true;
        std::wstring mp = cfg.MountPoint();
        std::memset(mount_bytes_, 0, sizeof(mount_bytes_));
        size_t n = mp.size();
        if (n > kFsMountPointMaxWchars - 1) n = kFsMountPointMaxWchars - 1;
        std::memcpy(mount_bytes_, mp.data(), n * sizeof(uint16_t));
    }

    void HandleCode(uint32_t code) {
        if (code == kServerPollCompletion) return;   /* op already completed */

        auto& mmu = emu_.Get<CerfVirtGuestMem>();
        CerfVirt::ServerPB pb;
        if (!mmu.ReadBlob(serverpb_va_, &pb, sizeof(pb)) ||
            pb.fStructureSize != sizeof(pb)) {
            result_ = kErrorGeneralFailure;
            return;
        }

#if CERF_DEV_MODE
        LOG(GuestAdditions, "[FolderShare] >> op=0x%X name='%ls' idx=%d tid=0x%X\n",
            code, reinterpret_cast<const wchar_t*>(pb.fLfn.fName),
            (int)pb.fIndex, pb.fFindTransactionID);
#endif
        uint32_t r;
        if (FolderShareFiles::Owns(code))
            r = emu_.Get<FolderShareFiles>().Run(code, pb);
        else if (FolderShareDir::Owns(code))
            r = emu_.Get<FolderShareDir>().Run(code, pb);
        else
            r = kErrorInvalidFunction;

        mmu.WriteBlob(serverpb_va_, &pb, sizeof(pb));
        result_ = r;
#if CERF_DEV_MODE
        LOG(GuestAdditions, "[FolderShare] op=0x%X name='%ls' nlen=%u attr=0x%X sz=%u "
            "time=0x%X ctime=0x%X h=%u pos=%u result=0x%X\n",
            code, reinterpret_cast<const wchar_t*>(pb.fLfn.fName), pb.fLfn.fNameLength,
            pb.fFileAttributes, pb.fSize, pb.fFileTimeDate, pb.fFileCreateTimeDate,
            pb.fHandle, pb.fPosition, r);
#endif
    }

    uint32_t serverpb_va_ = 0;
    uint32_t last_code_   = 0;
    uint32_t result_      = 0;
    uint32_t mount_gen_   = 0;
    bool     mount_inited_ = false;
    uint8_t  mount_bytes_[kMountBytes];
};

REGISTER_SERVICE(CerfVirtFolderShare);

}  /* namespace */

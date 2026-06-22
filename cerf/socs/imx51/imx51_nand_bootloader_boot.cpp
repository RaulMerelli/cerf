#define NOMINMAX

#include "../../boot/boot_mode.h"

#include "../../boards/board_detector.h"
#include "../../boot/guest_cold_boot.h"
#include "../../boot/sec_flash.h"
#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../cpu/emulated_memory.h"
#include "../guest_cpu_reset.h"

#include <algorithm>
#include <array>
#include <cstdint>

namespace {

/* i.MX51 NAND mask-ROM boot: the position-independent first-stage stub runs in
   place from the NFC internal RAM, so the boot ROM stages the image's first
   pages at the NFC RAM base and enters at flash_header.jump_vector. */
constexpr uint32_t kAppCodeBarker  = 0xB1u;        /* imximage.h APP_CODE_BARKER  */
constexpr uint32_t kFlashHdrOffset = 0x400u;       /* imximage.h FLASH_OFFSET_NAND */
constexpr uint32_t kNfcRamBase     = 0xCFFF0000u;  /* RM Table 2-1 / Table 45-4   */
constexpr uint32_t kNfcRamSize     = 0x1000u;      /* one staged 4 KB NAND page    */
constexpr uint32_t kIramBase       = 0x1FFE0000u;  /* RM Figure 9-1               */
constexpr uint32_t kIramEnd        = 0x20000000u;
constexpr uint32_t kInitStackPa    = 0x1FFFC000u;  /* IRAM; the stub sets its own SP */

constexpr uint64_t kHeaderScanBytes = 1u * 1024 * 1024;

struct FlashHeaderV1 {
    uint32_t jump_vector;
    uint32_t barker;
    uint32_t csf;
    uint32_t dcd_ptr_ptr;
    uint32_t super_root_key;
    uint32_t dcd_ptr;
    uint32_t app_dest;
};

class Imx51NandBootloaderBoot : public BootMode {
public:
    using BootMode::BootMode;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardDetector>();
        if (!bd || bd->GetSoc() != SocFamily::iMX51) return false;
        auto* sf = emu_.TryGet<SecFlash>();
        return sf && sf->IsPresent();
    }

    void OnReady() override {
        auto& sf = emu_.Get<SecFlash>();

        uint64_t      hdr_off = 0;
        FlashHeaderV1 hdr{};
        if (!FindHeader(sf, hdr_off, hdr)) {
            LOG(Caution, "Imx51NandBootloaderBoot: no flash_header_v1 in the "
                         "first %llu KB of NAND flash\n",
                static_cast<unsigned long long>(kHeaderScanBytes / 1024));
            CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
        }

        image_base_ = hdr_off - kFlashHdrOffset;
        entry_pa_   = hdr.jump_vector;
        Stage();
        emu_.Get<GuestColdBoot>().RegisterReplay([this] { Stage(); });
        emu_.Get<GuestCpuReset>().RegisterResetListener([this] { Stage(); });

        LOG(Boot, "Imx51NandBootloaderBoot: flash_header @ flash 0x%llX "
                  "(image base 0x%llX) -> staged %u B at NFC RAM 0x%08X, "
                  "entry 0x%08X, app_dest 0x%08X\n",
            static_cast<unsigned long long>(hdr_off),
            static_cast<unsigned long long>(image_base_),
            kNfcRamSize, kNfcRamBase, entry_pa_, hdr.app_dest);
    }

    uint32_t ColdEntryPa() override { return entry_pa_; }
    uint32_t ColdStackPa() override { return kInitStackPa; }

private:
    bool FindHeader(SecFlash& sf, uint64_t& hdr_off, FlashHeaderV1& out) {
        const uint64_t limit = std::min<uint64_t>(kHeaderScanBytes, sf.FlashSize());
        for (uint64_t off = kFlashHdrOffset;
             off + sizeof(FlashHeaderV1) <= limit; off += kFlashHdrOffset) {
            FlashHeaderV1 h{};
            if (sf.ReadFlash(off, &h, sizeof(h)) != sizeof(h)) break;
            if (h.barker != kAppCodeBarker)                          continue;
            if (h.jump_vector < kNfcRamBase ||
                h.jump_vector >= kNfcRamBase + kNfcRamSize)          continue;
            if (h.app_dest < kIramBase || h.app_dest >= kIramEnd)    continue;
            hdr_off = off;
            out     = h;
            return true;
        }
        return false;
    }

    void Stage() {
        auto& sf  = emu_.Get<SecFlash>();
        auto& mem = emu_.Get<EmulatedMemory>();
        std::array<uint8_t, kNfcRamSize> buf{};
        const size_t got = sf.ReadFlash(image_base_, buf.data(), buf.size());
        mem.CopyIn(kNfcRamBase, buf.data(), static_cast<uint32_t>(got));
    }

    uint64_t image_base_ = 0;
    uint32_t entry_pa_   = 0;
};

}  /* namespace */

REGISTER_SERVICE_AS(Imx51NandBootloaderBoot, BootMode);

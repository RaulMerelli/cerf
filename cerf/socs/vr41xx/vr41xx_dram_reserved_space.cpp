#include "../../peripherals/peripheral_base.h"

#include "../../boards/board_context.h"
#include "../../boards/page_table_builder.h"
#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../peripherals/peripheral_dispatcher.h"

#include <atomic>
#include <cstdint>

namespace {

/* Unpopulated DRAM above the board's populated top, up to 0x04000000: VR4102 UM
   Table 5-6 (DRAM 0x0-0x03FFFFFF; 0x04000000-0x09FFFFFF reserved) + VR4121 UM 11.4.7
   (0x04000000-0x09FFFFFF illegal-access). Read by the OAL RAM-sizing probe
   (Casio nk.exe sub_9F0B7200). */
constexpr uint32_t kReservedEnd = 0x04000000u;

class Vr41xxDramReservedSpace : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        if (!bd) return false;
        const SocFamily soc = bd->GetSoc();
        return soc == SocFamily::VR4102 || soc == SocFamily::VR4121;
    }

    void OnReady() override {
        uint32_t top = 0;
        for (const auto& r : emu_.Get<PageTableBuilder>().CachedDramRegions()) {
            const uint32_t r_top = r.pa_base + r.size;
            if (r_top > top) top = r_top;
        }
        base_ = top;
        size_ = (top < kReservedEnd) ? (kReservedEnd - top) : 0u;
        if (size_ != 0u) emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t MmioBase() const override { return base_; }
    uint32_t MmioSize() const override { return size_; }

    uint8_t  ReadByte (uint32_t addr) override { NoteAccess(addr); return 0; }
    uint16_t ReadHalf (uint32_t addr) override { NoteAccess(addr); return 0; }
    uint32_t ReadWord (uint32_t addr) override { NoteAccess(addr); return 0; }
    uint64_t ReadDword(uint32_t addr) override { NoteAccess(addr); return 0; }
    void WriteByte (uint32_t addr, uint8_t)  override { NoteAccess(addr); }
    void WriteHalf (uint32_t addr, uint16_t) override { NoteAccess(addr); }
    void WriteWord (uint32_t addr, uint32_t) override { NoteAccess(addr); }
    void WriteDword(uint32_t addr, uint64_t) override { NoteAccess(addr); }

private:
    void NoteAccess(uint32_t addr) {
#if CERF_DEV_MODE
        if (!logged_.exchange(true, std::memory_order_relaxed)) {
            LOG(Mem, "Vr41xxDramReservedSpace: access to unpopulated DRAM "
                     "pa=0x%08X (>= 0x%08X); reads 0 / writes dropped\n", addr, base_);
        }
#else
        (void)addr;
#endif
    }
    uint32_t base_ = 0;
    uint32_t size_ = 0;
    std::atomic<bool> logged_{false};
};

}  /* namespace */

REGISTER_SERVICE(Vr41xxDramReservedSpace);

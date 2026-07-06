#include "../../peripherals/peripheral_base.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../boards/board_context.h"
#include "../../peripherals/peripheral_dispatcher.h"

#include <atomic>
#include <cstdint>

namespace {

/* VR4102 DRAM space is one chip-select (UM Table 5-6); the MobilePro 700
   populates 8 MB (banks 0-1, PA 0-0x7FFFFF, UM Table 5-12 16-Mbit). Unpopulated
   banks (0x800000+) float - reads 0, writes dropped - and the OAL RAM-sizing
   probe in nk.exe start() reads/writes them, so this range decodes non-faulting. */
constexpr uint32_t kBase = 0x00800000u;
constexpr uint32_t kSize = 0x04000000u - 0x00800000u;  /* to end of DRAM space */

class Vr4102DramReservedSpace : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSoc() == SocFamily::VR4102;
    }
    void OnReady() override { emu_.Get<PeripheralDispatcher>().Register(this); }

    uint32_t MmioBase() const override { return kBase; }
    uint32_t MmioSize() const override { return kSize; }

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
            LOG(Mem, "Vr4102DramReservedSpace: access to unpopulated DRAM bank "
                     "pa=0x%08X (>8 MB); reads 0 / writes dropped\n", addr);
        }
#else
        (void)addr;
#endif
    }
    std::atomic<bool> logged_{false};
};

}  /* namespace */

REGISTER_SERVICE(Vr4102DramReservedSpace);

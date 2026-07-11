#include "../../core/cerf_emulator.h"
#include "../../socs/pr31x00/pr31x00_io_pins.h"
#include "../board_context.h"

#include <cstdint>

namespace {

/* IO[0] M-Module attached, active high. nk.1.exe sub_9FB11194 ("Checking for
   M-Module Attached") clears IODIREC[0] then takes the "No M-Module" branch on
   (IODIN & 1) != 1; nk.exe sub_9F40F688 gates the whole Iceberg-ASIC bring-up on
   the same bit. This board fits the M-Module, so its Iceberg answers CS2. */
constexpr uint32_t kMModuleAttached = 1u << 0;

/* IO[4] serial DCD, active low. serial.dll sub_1EB1F04 (GetModemStatus, built-in
   port) sets MS_RLSD_ON when (IODIN & 0x10) == 0. No carrier. */
constexpr uint32_t kSerialDcd = 1u << 4;

/* IO[5] and IO[6] Miniature Card detect for slots 1 and 2, active low. nk.1.exe
   sub_9FB1A0C4 returns IODIN & 0x20 for slot 1 and IODIN & 0x40 for slot 2, and
   sub_9FB0F424 prints "No Mini Card in Slot N" when the bit is set. Both empty. */
constexpr uint32_t kMiniCardSlot1 = 1u << 5;
constexpr uint32_t kMiniCardSlot2 = 1u << 6;

/* IO[1] carries an M-Module socket status line (nk.1.exe sub_9FB2BC6C, pcmcia.dll)
   that only the absent M-Module drives; IO[2] has no reader in the ROM; IO[3] is
   an output, driven high by the suspend path (nk.exe sub_9F40E5E0 writes
   IO_CTL = 0x10080800, IODIREC = IODOUT = 0x08). */
class PhilipsVelo1IoPins : public Pr31x00IoPins {
public:
    using Pr31x00IoPins::Pr31x00IoPins;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoard() == Board::PhilipsVelo1;
    }

    uint32_t IoDin() const override {
        return kMModuleAttached | kSerialDcd | kMiniCardSlot1 | kMiniCardSlot2;
    }
};

}  /* namespace */

REGISTER_SERVICE_AS(PhilipsVelo1IoPins, Pr31x00IoPins);

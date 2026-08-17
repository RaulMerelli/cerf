#include "siemens_mp377_aspc2.h"

#include "../../core/cerf_emulator.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../state/state_stream.h"
#include "../board_context.h"

namespace siemens_mp377 {

bool SiemensMp377Aspc2::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetBoard() == Board::SiemensMP377;
}

void SiemensMp377Aspc2::OnReady() {
    registers_.fill(0u);
    /* Siemens ASPC2 Hardware User Description V2.4, section 1.5.2.1:
       the ASIC revision is read at byte address 0x0B.  MP377 S7pbhmix.dll
       sub_2A530D0 identifies its supported part as "ASPC2 (E2+)" and rejects
       revision codes below 4. */
    registers_[kVersionOffset] = kE2PlusVersion;
    emu_.Get<PeripheralDispatcher>().Register(this);
}

uint32_t SiemensMp377Aspc2::MmioBase() const { return kMp377Aspc2Base; }
uint32_t SiemensMp377Aspc2::MmioSize() const { return kMp377Aspc2Size; }

uint8_t SiemensMp377Aspc2::ReadByte(uint32_t addr) {
    return registers_[addr - MmioBase()];
}

uint16_t SiemensMp377Aspc2::ReadHalf(uint32_t addr) {
    return static_cast<uint16_t>(ReadByte(addr) |
                                 (ReadByte(addr + 1u) << 8));
}

uint32_t SiemensMp377Aspc2::ReadWord(uint32_t addr) {
    return static_cast<uint32_t>(ReadByte(addr) |
                                 (ReadByte(addr + 1u) << 8) |
                                 (ReadByte(addr + 2u) << 16) |
                                 (ReadByte(addr + 3u) << 24));
}

void SiemensMp377Aspc2::WriteByte(uint32_t addr, uint8_t value) {
    const uint32_t offset = addr - MmioBase();
    if (offset != kVersionOffset) {
        registers_[offset] = value;
    }
}

void SiemensMp377Aspc2::WriteHalf(uint32_t addr, uint16_t value) {
    WriteByte(addr, static_cast<uint8_t>(value));
    WriteByte(addr + 1u, static_cast<uint8_t>(value >> 8));
}

void SiemensMp377Aspc2::WriteWord(uint32_t addr, uint32_t value) {
    WriteByte(addr, static_cast<uint8_t>(value));
    WriteByte(addr + 1u, static_cast<uint8_t>(value >> 8));
    WriteByte(addr + 2u, static_cast<uint8_t>(value >> 16));
    WriteByte(addr + 3u, static_cast<uint8_t>(value >> 24));
}

void SiemensMp377Aspc2::SaveState(StateWriter& w) {
    w.WriteBytes(registers_.data(), registers_.size());
}

void SiemensMp377Aspc2::RestoreState(StateReader& r) {
    r.ReadBytes(registers_.data(), registers_.size());
}

REGISTER_SERVICE(SiemensMp377Aspc2);

}  // namespace siemens_mp377


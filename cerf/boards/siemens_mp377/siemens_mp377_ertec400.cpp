#include "siemens_mp377_ertec400.h"
#include "siemens_mp377_ertec400_model.h"
#include "siemens_mp377_ertec400_nrt.h"

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../peripherals/peripheral_base.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../state/state_stream.h"

#include <cstdint>

namespace {

bool IsBar3(uint32_t address) {
    return address >= siemens_mp377::kErtecBar3Base &&
           address < siemens_mp377::kErtecBar3End;
}

bool IsSmallIrtWindow(uint32_t address) {
    return address >= siemens_mp377::kErtecSmallBarsBase &&
           address < siemens_mp377::kErtecSmallBarsEnd;
}

uint32_t ErtecIrtOffset(uint32_t address) {
    using namespace siemens_mp377;
    if (IsBar3(address)) {
        const uint32_t raw = address - kErtecBar3Base;
        /* ERTEC400 manual, memory section: 2 MB IRT aperture and 16/32-bit mirrors. */
        return raw < 0x00600000u ? raw % kErtecIrtApertureSize : 0xFFFFFFFFu;
    }
    if (IsSmallIrtWindow(address)) return address - kErtecSmallBarsBase;
    return 0xFFFFFFFFu;
}

bool IsNrtDmacCommand(uint32_t offset) {
    using namespace siemens_mp377;
    if (offset < kErtecNrtDmacBaseOffset) return false;
    const uint32_t relative = offset - kErtecNrtDmacBaseOffset;
    return relative % kErtecNrtDmacStride == 0u &&
           relative / kErtecNrtDmacStride < kErtecNrtDmacPortCount;
}

bool IsNrtDmacAddress(uint32_t offset) {
    using namespace siemens_mp377;
    if (offset < kErtecNrtDmacBaseOffset) return false;
    const uint32_t relative = offset - kErtecNrtDmacBaseOffset;
    const uint32_t slot = relative / kErtecNrtDmacStride;
    const uint32_t within = relative % kErtecNrtDmacStride;
    return slot < kErtecNrtDmacPortCount && (within == 4u || within == 8u);
}

bool IsIrtCommandLatch(uint32_t offset) {
    using namespace siemens_mp377;
    return offset == kErtecIrtControlOffset ||
           offset == kErtecIrtControlOffset + 0x04u ||
           offset == 0x00013048u || offset == 0x00013404u;
}

bool IsDriverVisibleRegister(uint32_t offset) {
    using namespace siemens_mp377;
    if (IsNrtDmacCommand(offset) || IsNrtDmacAddress(offset)) return true;
    switch (offset) {
        case kErtecResetControlOffset:
        case 0x00015000u:
        case 0x00015004u:
        case kErtecSerPrimCommandOffset:
        case kErtecSerSecCommandOffset:
        case kErtecSerConfCommandOffset:
        case kErtecFlowControlOffset - 4u:
        case kErtecFlowControlOffset:
        case kErtecIrtControlOffset:
        case kErtecIrtControlOffset + 0x04u:
        case 0x00013048u:
        case 0x00013404u:
        case 0x00011000u:
        case 0x00011004u:
        case 0x00011008u:
        case 0x0001100Cu:
        case 0x00011010u:
        case 0x00011014u:
        case 0x00011018u:
        case 0x0001101Cu:
        case 0x00011020u:
        case 0x00011024u:
        case 0x00011028u:
        case 0x0001102Cu:
        case 0x00011030u:
        case 0x00011034u:
        case 0x00011038u:
        case 0x00011400u:
        case 0x00011404u:
        case 0x00011408u:
        case 0x00011410u:
        case 0x00011414u:
        case 0x00011418u:
        case 0x0001141Cu:
        case 0x00011424u:
        case 0x00012000u:
        case 0x00012004u:
        case 0x0001500Cu:
        case 0x00015014u:
        case 0x0001501Cu:
        case 0x00016000u:
        case 0x00016004u:
        case 0x00016008u:
        case 0x0001600Cu:
        case 0x00016010u:
        case 0x00016014u:
        case 0x00016018u:
        case 0x0001601Cu:
        case 0x00016020u:
        case 0x00016024u:
        case 0x00016028u:
        case 0x0001602Cu:
        case 0x00016030u:
        case 0x00016034u:
        case 0x00016038u:
        case 0x0001603Cu:
        case 0x00016040u:
        case 0x00016044u:
        case 0x00019010u:
        case 0x00019024u:
        case 0x00019028u:
        case 0x0001902Cu:
        case 0x0001903Cu:
        case 0x00019040u:
        case 0x00019044u:
        case 0x00019048u:
        case 0x00019050u:
        case 0x00019054u:
        case 0x00016414u:
        case 0x00016418u:
        case 0x00016420u:
        case 0x00016424u:
        case 0x00016428u:
        case kErtecIrqStatusLoOffset:
        case kErtecIrqStatusHiOffset:
        case kErtecIrqAckOffset:
        case kErtecIrtStartOffset:
        case kErtecIrtStartOffset + 4u:
        case kErtecSwiControlOffset:
        case kErtecSwiStatusOffset:
        case kErtecEddHwTypeOffset:
            return true;
        default:
            return false;
    }
}

uint32_t ErtecRegisterOffset(uint32_t address) {
    using namespace siemens_mp377;
    const uint32_t offset = ErtecIrtOffset(address);
    if (offset == 0xFFFFFFFFu) return offset;
    if (offset >= kErtecRegisterAliasDelta) {
        const uint32_t aliased = offset - kErtecRegisterAliasDelta;
        if (IsDriverVisibleRegister(aliased)) return aliased;
    }
    return offset;
}

bool IsReadOnlyLinkStatus(uint32_t offset) {
    return offset == 0x0001500Cu || offset == 0x00015014u ||
           offset == 0x0001501Cu;
}

class SiemensMp377Ertec400BarWindow : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* board = emu_.TryGet<BoardContext>();
        return board && board->GetBoard() == Board::SiemensMP377;
    }

    void OnReady() override {
        emu_.Get<SiemensMp377Ertec400Model>();
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint8_t ReadByte(uint32_t address) override {
        const uint32_t word = ReadWord(address & ~3u);
        return static_cast<uint8_t>((word >> ((address & 3u) * 8u)) & 0xFFu);
    }

    uint16_t ReadHalf(uint32_t address) override {
        const uint32_t word = ReadWord(address & ~3u);
        return static_cast<uint16_t>((word >> ((address & 2u) * 8u)) & 0xFFFFu);
    }

    uint32_t ReadWord(uint32_t address) override {
        auto& model = emu_.Get<SiemensMp377Ertec400Model>();
        model.BeginAccess();
        const uint32_t offset = ErtecRegisterOffset(address);
        if (offset == 0xFFFFFFFFu) return 0u;
        if (offset == siemens_mp377::kErtecEddHwTypeOffset)
            return siemens_mp377::kErtecEddHwTypeErtec400Rev5;
        if (IsReadOnlyLinkStatus(offset)) return model.DefaultRead(offset);
        if (offset == siemens_mp377::kErtecIrqStatusHiOffset)
            return emu_.Get<SiemensMp377Ertec400Nrt>().InterruptStatusHigh();
        if (offset == siemens_mp377::kErtecIrqStatusLoOffset)
            return model.DefaultRead(offset);

        uint32_t value = 0;
        if (offset == siemens_mp377::kErtecSerPrimCommandOffset &&
            model.ConsumePrimaryReadback(value)) return value;
        if (offset == siemens_mp377::kErtecSerSecCommandOffset &&
            model.ConsumeSecondaryReadback(value)) return value;
        if (offset == 0x00011414u) return model.FreeRunningCounter10ns();

        if (model.GetWord(offset, value)) {
            if (offset == 0x00015004u) return value & ~0x00000800u;
            if (IsNrtDmacCommand(offset)) return value & ~0x00000002u;
            if (IsIrtCommandLatch(offset)) return 0u;
            return value;
        }

        value = model.DefaultRead(offset);
        if (offset == siemens_mp377::kErtecBootReadyOffset ||
            offset == siemens_mp377::kErtecSwiStatusOffset ||
            offset == siemens_mp377::kErtecSwiControlOffset ||
            offset == siemens_mp377::kErtecSerPrimCommandOffset ||
            offset == siemens_mp377::kErtecSerSecCommandOffset ||
            offset == siemens_mp377::kErtecSerConfCommandOffset ||
            offset == siemens_mp377::kErtecIrtControlOffset ||
            offset == siemens_mp377::kErtecIrtControlOffset + 0x04u ||
            offset == 0x00013048u || offset == 0x00013404u ||
            offset == 0x00015000u || offset == 0x00015004u) {
            model.SetWord(offset, value);
        }
        return value;
    }

    void WriteByte(uint32_t address, uint8_t value) override {
        const uint32_t aligned = address & ~3u;
        const uint32_t shift = (address & 3u) * 8u;
        const uint32_t mask = 0xFFu << shift;
        WriteWord(aligned, (ReadWord(aligned) & ~mask) |
                           (static_cast<uint32_t>(value) << shift));
    }

    void WriteHalf(uint32_t address, uint16_t value) override {
        const uint32_t aligned = address & ~3u;
        const uint32_t shift = (address & 2u) * 8u;
        const uint32_t mask = 0xFFFFu << shift;
        WriteWord(aligned, (ReadWord(aligned) & ~mask) |
                           (static_cast<uint32_t>(value) << shift));
    }

    void WriteWord(uint32_t address, uint32_t value) override {
        auto& model = emu_.Get<SiemensMp377Ertec400Model>();
        model.BeginAccess();
        const uint32_t offset = ErtecRegisterOffset(address);
        if (offset == 0xFFFFFFFFu ||
            offset == siemens_mp377::kErtecEddHwTypeOffset ||
            IsReadOnlyLinkStatus(offset) || offset == 0x00011414u) return;

        if (offset == siemens_mp377::kErtecResetControlOffset) {
            model.SetWord(offset, value);
            if ((value & 0x00000002u) != 0u) {
                model.PublishIdleInitState();
                emu_.Get<SiemensMp377Ertec400Nrt>().Reset();
            }
            return;
        }
        if (offset == siemens_mp377::kErtecSwiControlOffset) {
            model.ApplySwitchControl(value);
            return;
        }
        if (offset == siemens_mp377::kErtecSwiStatusOffset) {
            model.PublishSwitchReady(value);
            return;
        }
        if (offset == siemens_mp377::kErtecIrqAckOffset) {
            model.SetWord(offset, value);
            emu_.Get<SiemensMp377Ertec400Nrt>().AcknowledgeInterrupt();
            return;
        }
        if (offset == 0x00015004u) {
            model.WriteMdioControl(value);
            return;
        }
        if (offset == 0x00015000u) {
            model.SetWord(offset, value & 0x0000FFFFu);
            return;
        }
        if (offset == siemens_mp377::kErtecSerPrimCommandOffset) {
            model.CompletePrimaryCommand(value);
            return;
        }
        if (offset == siemens_mp377::kErtecSerSecCommandOffset) {
            model.CompleteSecondaryCommand(value);
            return;
        }
        if (offset == siemens_mp377::kErtecSerConfCommandOffset) {
            model.SetWord(offset, value & ~siemens_mp377::kErtecSerCommandActiveBit);
            return;
        }
        if (IsIrtCommandLatch(offset)) {
            model.SetWord(offset, 0u);
            return;
        }
        if (IsNrtDmacCommand(offset)) {
            model.SetWord(offset, value & ~0x00000002u);
            const uint32_t channel =
                (offset - siemens_mp377::kErtecNrtDmacBaseOffset) /
                siemens_mp377::kErtecNrtDmacStride;
            emu_.Get<SiemensMp377Ertec400Nrt>().ExecuteCommand(channel, value);
            return;
        }
        if (IsNrtDmacAddress(offset)) {
            model.SetWord(offset, value);
            const uint32_t relative = offset - siemens_mp377::kErtecNrtDmacBaseOffset;
            const uint32_t channel = relative / siemens_mp377::kErtecNrtDmacStride;
            const bool receive = relative % siemens_mp377::kErtecNrtDmacStride == 8u;
            emu_.Get<SiemensMp377Ertec400Nrt>().ConfigureRingAddress(
                channel, receive, value);
            return;
        }
        model.SetWord(offset, value);
    }
};

class SiemensMp377Ertec400SmallBars final : public SiemensMp377Ertec400BarWindow {
public:
    using SiemensMp377Ertec400BarWindow::SiemensMp377Ertec400BarWindow;
    uint32_t MmioBase() const override { return siemens_mp377::kErtecSmallWindowBase; }
    uint32_t MmioSize() const override { return siemens_mp377::kErtecSmallWindowSize; }

    void SaveState(StateWriter& writer) override {
        emu_.Get<SiemensMp377Ertec400Model>().SaveState(writer);
        emu_.Get<SiemensMp377Ertec400Nrt>().SaveState(writer);
    }

    void RestoreState(StateReader& reader) override {
        emu_.Get<SiemensMp377Ertec400Model>().RestoreState(reader);
        emu_.Get<SiemensMp377Ertec400Nrt>().RestoreState(reader);
    }
};

class SiemensMp377Ertec400Bar3 final : public SiemensMp377Ertec400BarWindow {
public:
    using SiemensMp377Ertec400BarWindow::SiemensMp377Ertec400BarWindow;
    uint32_t MmioBase() const override { return siemens_mp377::kErtecBar3WindowBase; }
    uint32_t MmioSize() const override { return siemens_mp377::kErtecBar3WindowSize; }
};

}

REGISTER_SERVICE(SiemensMp377Ertec400SmallBars);
REGISTER_SERVICE(SiemensMp377Ertec400Bar3);


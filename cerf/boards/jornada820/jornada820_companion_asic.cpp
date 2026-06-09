#include "jornada820_companion_asic.h"

#include "../../core/cerf_emulator.h"
#include "../../boards/board_detector.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../socs/sa11xx/sa11xx_gpio.h"

namespace {
/* GlidePad IRQ = SA-1100 GPIO14: runtime-proven the only edge GPIO whose pulse
   wakes glidepad.dll's InterruptInitialize(29) thread sub_12B1EFC. Other values
   (e.g. GPIO1, masked in ICMR) never deliver and the touchpad goes dead. */
constexpr uint32_t kGpio = 14u;
}  /* namespace */

bool Jornada820CompanionAsic::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardDetector>();
    return bd && bd->GetBoard() == Board::Jornada820;
}

void Jornada820CompanionAsic::OnReady() {
    store_.assign(MmioSize(), 0u);
    emu_.Get<PeripheralDispatcher>().Register(this);
}

uint8_t Jornada820CompanionAsic::ReadByte(uint32_t addr) {
    const uint32_t o = addr - MmioBase();
    if (o == kPs2Status) return Ps2StatusByte();
    if (o == kPs2Data)   return mouse_.ReadData();
    return store_[o];
}

uint16_t Jornada820CompanionAsic::ReadHalf(uint32_t addr) {
    const uint32_t o = addr - MmioBase();
    return static_cast<uint16_t>(store_[o] | (store_[o + 1] << 8));
}

uint32_t Jornada820CompanionAsic::ReadWord(uint32_t addr) {
    const uint32_t o = addr - MmioBase();
    if (o == kPs2Status) return Ps2StatusByte();
    if (o == kPs2Data)   return mouse_.ReadData();
    return static_cast<uint32_t>(store_[o]) | (store_[o + 1] << 8) |
           (store_[o + 2] << 16) | (store_[o + 3] << 24);
}

void Jornada820CompanionAsic::WriteByte(uint32_t addr, uint8_t v) {
    const uint32_t o = addr - MmioBase();
    if (o == kPs2Data) { mouse_.WriteCommand(v); return; }
    store_[o] = v;
}

void Jornada820CompanionAsic::WriteHalf(uint32_t addr, uint16_t v) {
    const uint32_t o = addr - MmioBase();
    store_[o]     = static_cast<uint8_t>(v);
    store_[o + 1] = static_cast<uint8_t>(v >> 8);
}

void Jornada820CompanionAsic::WriteWord(uint32_t addr, uint32_t v) {
    const uint32_t o = addr - MmioBase();
    if (o == kPs2Data) { mouse_.WriteCommand(static_cast<uint8_t>(v)); return; }
    store_[o]     = static_cast<uint8_t>(v);
    store_[o + 1] = static_cast<uint8_t>(v >> 8);
    store_[o + 2] = static_cast<uint8_t>(v >> 16);
    store_[o + 3] = static_cast<uint8_t>(v >> 24);
}

void Jornada820CompanionAsic::RaiseIrq() {
    auto& gpio = emu_.Get<Sa11xxGpio>();
    gpio.DriveInputPin(kGpio, false);
    gpio.DriveInputPin(kGpio, true);   /* rising edge -> GEDR / INTC */
}

REGISTER_SERVICE(Jornada820CompanionAsic);

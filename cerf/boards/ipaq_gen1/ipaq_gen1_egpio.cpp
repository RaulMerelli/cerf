#include "ipaq_gen1_egpio.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../boards/board_context.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../state/state_stream.h"
#include "ipaq_gen1_egpio_sink.h"

bool IpaqGen1Egpio::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetBoard() == Board::IpaqGen1;
}

void IpaqGen1Egpio::OnReady() {
    emu_.Get<PeripheralDispatcher>().Register(this);
}

void IpaqGen1Egpio::NotifySink() {
    if (auto* sink = emu_.TryGet<IpaqGen1EgpioSink>()) {
        sink->OnEgpioChanged(latched_.load(std::memory_order_acquire));
    }
}

uint8_t IpaqGen1Egpio::ReadByte(uint32_t addr) {
    const uint32_t off = addr - MmioBase();
    return static_cast<uint8_t>((latched_.load(std::memory_order_acquire) >> (8 * off)) & 0xFFu);
}

uint32_t IpaqGen1Egpio::ReadWord(uint32_t addr) {
    if (addr != MmioBase()) HaltUnsupportedAccess("ReadWord", addr, 0);
    return latched_.load(std::memory_order_acquire);
}

void IpaqGen1Egpio::WriteByte(uint32_t addr, uint8_t value) {
    const uint32_t off    = addr - MmioBase();
    const uint32_t shift  = 8 * off;
    const uint32_t mask   = 0xFFu << shift;
    const uint32_t before = latched_.load(std::memory_order_acquire);
    const uint32_t after  = (before & ~mask) | (static_cast<uint32_t>(value) << shift);
    latched_.store(after, std::memory_order_release);
    LOG(Periph, "[IpaqGen1Egpio] W8 +%u = 0x%02X -> latch 0x%08X (audio output %s)\n",
        off, value, after, (after & kAudioOutputEnable) ? "on" : "silenced");
    NotifySink();
}

void IpaqGen1Egpio::WriteWord(uint32_t addr, uint32_t value) {
    if (addr != MmioBase()) HaltUnsupportedAccess("WriteWord", addr, value);
    latched_.store(value, std::memory_order_release);
    LOG(Periph, "[IpaqGen1Egpio] W32 = 0x%08X (audio output %s)\n",
        value, (value & kAudioOutputEnable) ? "on" : "silenced");
    NotifySink();
}

void IpaqGen1Egpio::SaveState(StateWriter& w) {
    w.Write(latched_.load(std::memory_order_acquire));
}

void IpaqGen1Egpio::RestoreState(StateReader& r) {
    uint32_t v = 0;
    r.Read(v);
    latched_.store(v, std::memory_order_release);
}

REGISTER_SERVICE(IpaqGen1Egpio);

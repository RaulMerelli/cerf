#include "physical_bus.h"

#include "../core/cerf_emulator.h"
#include "../peripherals/peripheral_dispatcher.h"
#include "emulated_memory.h"

#include <cstring>

REGISTER_SERVICE(PhysicalBus);

bool PhysicalBus::Read(uint32_t pa, BusWidth width, uint32_t* out) {
    if (uint8_t* p = emu_.Get<EmulatedMemory>().TryTranslate(pa)) {
        *out = 0;
        std::memcpy(out, p, static_cast<uint32_t>(width));
        return true;
    }
    auto& disp = emu_.Get<PeripheralDispatcher>();
    if (!disp.IsPeripheralAddress(pa)) return false;
    switch (width) {
        case BusWidth::Byte: *out = disp.ReadByte(pa); break;
        case BusWidth::Half: *out = disp.ReadHalf(pa); break;
        case BusWidth::Word: *out = disp.ReadWord(pa); break;
    }
    return true;
}

bool PhysicalBus::Write(uint32_t pa, BusWidth width, uint32_t value) {
    if (uint8_t* p = emu_.Get<EmulatedMemory>().TryTranslateWrite(pa)) {
        std::memcpy(p, &value, static_cast<uint32_t>(width));
        return true;
    }
    auto& disp = emu_.Get<PeripheralDispatcher>();
    if (!disp.IsPeripheralAddress(pa)) return false;
    switch (width) {
        case BusWidth::Byte: disp.WriteByte(pa, static_cast<uint8_t>(value));  break;
        case BusWidth::Half: disp.WriteHalf(pa, static_cast<uint16_t>(value)); break;
        case BusWidth::Word: disp.WriteWord(pa, value);                        break;
    }
    return true;
}

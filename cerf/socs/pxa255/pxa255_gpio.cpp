#include "pxa255_gpio.h"

#include "../../boards/board_context.h"
#include "pxa255_id.h"
#include "../../core/cerf_emulator.h"

bool Pxa255Gpio::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetSocId() == SocId::Pxa255;
}

REGISTER_SERVICE(Pxa255Gpio);

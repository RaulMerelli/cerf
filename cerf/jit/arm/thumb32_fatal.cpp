#include "thumb32_fatal.h"

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../core/fatal.h"
#include "decoded_insn.h"

REGISTER_SERVICE(Thumb32Fatal);

bool Thumb32Fatal::ShouldRegister() {
    return emu_.Get<BoardContext>().GetCpuArch() == CpuArch::Arm;
}

void Thumb32Fatal::Unimplemented(const char* what, const DecodedInsn* insn,
                                 uint32_t op) {
    emu_.Get<Fatal>().Die("Thumb32Decoder: %s not implemented, op=0x%08X "
                          "at guest PC 0x%08X\n",
                          what, op, insn->guest_address);
}

#include <cstddef>

#include "../cpu_state.h"
#include "../place_fns.h"
#include "../../x86_emit.h"
#include "../../x86_emit_alu.h"

/* QEMU accel/tcg/cpu-exec.c cpu_loop_exec_tb: the exit request is polled
   before the next translation block; Dolphin CoreTiming: the block chain ends
   when the executed cycles reach the slice's end. */
uint8_t* EmitDispatcherExitPoll(uint8_t* cursor, uint8_t* (&to_dispatcher)[2]) {
    using namespace x86;
    EmitMovRegBaseDisp32(cursor, kEax, kStateReg,
        static_cast<int32_t>(offsetof(ArmCpuState, chain_exit_request)));
    EmitTestRegReg(cursor, kEax, kEax);
    to_dispatcher[0] = EmitJnzLabel32(cursor);
    EmitMovRegBaseDisp32(cursor, kEax, kStateReg,
        static_cast<int32_t>(offsetof(ArmCpuState, guest_cycle_counter)));
    EmitSubRegBaseDisp32(cursor, kEax, kStateReg,
        static_cast<int32_t>(offsetof(ArmCpuState, guest_cycle_deadline)));
    to_dispatcher[1] = EmitJnsLabel32(cursor);
    return cursor;
}

#pragma once

class ArmJit;

struct BlockContext {
    ArmJit*     jit;
    const void* sctlr_write_target;
    const void* raise_abort_data_helper_target;
};

#include "../../jit/coproc_emitter.h"

#include "../../core/cerf_emulator.h"
#include "../../jit/place_fns.h"
#include "../../boards/board_detector.h"

namespace {

class S3C2410CoprocEmitter : public CoprocEmitter {
public:
    using CoprocEmitter::CoprocEmitter;

    bool ShouldRegister() override {
        return emu_.Get<BoardDetector>().GetSoc() == SocFamily::S3C2410;
    }

    uint8_t* EmitRegisterTransfer(uint8_t*      cursor,
                                  DecodedInsn*  d,
                                  BlockContext* ctx) override {
        if (d->cp_num == 15) {
            return EmitCp15RegisterTransfer(cursor, d, ctx);
        }
        return EmitRaiseUndAndReturn(cursor, d, ctx);
    }

    uint8_t* EmitDataTransfer(uint8_t*      cursor,
                              DecodedInsn*  d,
                              BlockContext* ctx) override {
        return EmitRaiseUndAndReturn(cursor, d, ctx);
    }

    uint8_t* EmitDataOperation(uint8_t*      cursor,
                               DecodedInsn*  d,
                               BlockContext* ctx) override {
        return EmitRaiseUndAndReturn(cursor, d, ctx);
    }
};

}  /* namespace */

REGISTER_SERVICE_AS(S3C2410CoprocEmitter, CoprocEmitter);

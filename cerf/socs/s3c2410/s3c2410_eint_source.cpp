#include "s3c2410_eint_source.h"

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../core/log.h"

REGISTER_SERVICE(S3C2410EintSource);

bool S3C2410EintSource::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetSoc() == SocFamily::S3C2410;
}

void S3C2410EintSource::SetSink(S3C2410EintSink* sink) {
    if (sink_ && sink_ != sink) {
        LOG(Caution, "S3C2410EintSource::SetSink: a second sink registered\n");
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
    sink_ = sink;
}

void S3C2410EintSource::DriveEintPin(int eint, bool level) {
    if (!sink_) {
        LOG(Caution, "S3C2410EintSource::DriveEintPin: EINT%d with no sink\n", eint);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
    sink_->DriveEintPin(eint, level);
}

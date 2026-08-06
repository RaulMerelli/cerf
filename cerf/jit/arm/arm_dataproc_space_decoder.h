#pragma once

#include "../../core/service.h"

class ArmProcessorConfig;
struct DecodedInsn;
union  ArmOpcode;

/* Decoder for the Table A5-2 data-processing and miscellaneous space
   (ARM DDI 0406C.c, p. A5-196), including its Table A5-12 synchronization
   and Table A5-10/A5-11 extra load/store sub-spaces. */
class ArmDataprocSpaceDecoder : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    bool Decode(DecodedInsn* insn, ArmOpcode op);

private:
    bool DecodeMiscSpace(DecodedInsn* insn, ArmOpcode op);
    bool DecodeSyncSpace(DecodedInsn* insn, ArmOpcode op);
    bool DecodeLdrexStrex(DecodedInsn* insn, ArmOpcode op);
    bool DecodeExtraLoadStore(DecodedInsn* insn, ArmOpcode op);
    bool DecodeMsrImmHints(DecodedInsn* insn, ArmOpcode op);

    ArmProcessorConfig* processor_config_ = nullptr;
};

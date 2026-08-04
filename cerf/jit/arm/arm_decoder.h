#pragma once

#include "../../core/service.h"

class ArmProcessorConfig;
class NeonUnconditionalDecoder;
struct DecodedInsn;
union  ArmOpcode;

class ArmDecoder : public Service {
public:
    using Service::Service;

    void OnReady() override;

    bool DecodeArmBitfield(DecodedInsn* insn, ArmOpcode op);
    bool DecodeArmClass3Misc(DecodedInsn* insn, ArmOpcode op);
    bool DecodeArmLdrexStrex(DecodedInsn* insn, ArmOpcode op);
    bool DecodeArmUnconditional(DecodedInsn* insn, ArmOpcode op);

private:
    ArmProcessorConfig*       processor_config_           = nullptr;
    NeonUnconditionalDecoder* neon_unconditional_decoder_ = nullptr;
};

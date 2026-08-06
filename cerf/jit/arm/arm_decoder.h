#pragma once

#include "../../core/service.h"

class ArmBranchBlockSpaceDecoder;
class ArmDataprocSpaceDecoder;
class ArmMediaSpaceDecoder;
class ArmProcessorConfig;
class ArmSingleDataSpaceDecoder;
class ArmUnconditionalSpaceDecoder;
struct DecodedInsn;
union  ArmOpcode;

class ArmDecoder : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    bool DecodeArm(DecodedInsn* insn, ArmOpcode op);

private:
    ArmProcessorConfig*           processor_config_      = nullptr;
    ArmBranchBlockSpaceDecoder*   branch_block_decoder_  = nullptr;
    ArmDataprocSpaceDecoder*      dataproc_decoder_      = nullptr;
    ArmMediaSpaceDecoder*         media_decoder_         = nullptr;
    ArmSingleDataSpaceDecoder*    single_data_decoder_   = nullptr;
    ArmUnconditionalSpaceDecoder* unconditional_decoder_ = nullptr;
};

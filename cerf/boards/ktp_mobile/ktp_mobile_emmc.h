#pragma once

#include "../../peripherals/mmc/emmc_card_base.h"
#include "ktp_mobile_emmc_backing.h"

#include <memory>
#include <span>
#include <vector>

class KtpMobileEmmc final : public EmmcCardBase {
public:
    using EmmcCardBase::EmmcCardBase;
    ~KtpMobileEmmc() override;

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t SlotIndex() const override;

protected:
    SdCardCid                       Cid() const override;
    EmmcCsdFields                   Csd() const override;
    std::span<const EmmcExtCsdByte> ExtCsdProperties() const override;
    uint32_t                        SectorCount() const override;
    uint8_t                         ErasedMemCont() const override;

    void ReadBlock(uint32_t sector, uint8_t* out) override;
    void WriteBlock(uint32_t sector, const uint8_t* data) override;

private:
    std::vector<uint8_t>                data_;
    std::unique_ptr<KtpMobileEmmcBacking> backing_;
};

#pragma once

#include "ktp_mobile_f_module_model.h"
#include "../../core/virtual_timer_list.h"
#include "../../socs/imx6/imx6_ecspi_endpoint.h"

#include <array>
#include <cstddef>
#include <cstdint>

class StateReader;
class StateWriter;

/* Board owner for the Siemens STM32 F-module companion.  The portable model
   owns protocol/Flash state; this adapter owns only i.MX6 wiring and the
   synchronous SDMA staging needed to reproduce a streamed 272-byte burst. */
class KtpMobileFModuleDevice final : public Imx6EcspiEndpoint {
public:
    using Imx6EcspiEndpoint::Imx6EcspiEndpoint;

    bool ShouldRegister() override;
    void OnReady() override;
    void OnShutdown() override;
    uint32_t EcspiBase() const override { return 0x02010000u; }
    void StageDmaTransmit(uint32_t buffer_pa, uint32_t bytes) override;
    void StageDmaReceive(uint32_t buffer_pa, uint32_t bytes) override;
    bool HasStagedTransmit() const override { return dma_tx_ready_; }
    bool Exchange(uint32_t conreg, uint32_t configreg) override;

    bool ReadyLevel() const;
    void ObservePanelAcknowledge(bool high);
    void ObserveModuleReset(bool high);

    using ReadyChangedFn = void (*)(void*);
    void SetReadyChangedCallback(ReadyChangedFn fn, void* context);

    void SaveState(StateWriter& w);
    void RestoreState(StateReader& r);
    void PostRestore();

private:
    static constexpr uint32_t kTransferBytes =
        static_cast<uint32_t>(ktp_mobile::kWireTransactionBytes);

    void NotifyIfReadyChanged(bool old_ready);
    void FinishTransactionIfAcknowledged();
    void FlushDmaReceive();
    void ClearWireTransaction();
    void ArmCyclicReady();
    void OnCyclicReady();

    ktp_mobile::KtpMobileFModule model_;
    std::array<uint8_t, ktp_mobile::kWireTransactionBytes> dma_tx_{};
    std::array<uint8_t, ktp_mobile::kWireTransactionBytes> dma_rx_{};
    uint32_t dma_tx_bytes_ = 0;
    uint32_t dma_rx_buffer_pa_ = 0;
    uint32_t dma_rx_bytes_ = 0;
    bool dma_tx_ready_ = false;
    bool dma_rx_pending_ = false;
    bool dma_response_ready_ = false;
    bool serial_complete_ = false;
    bool model_cs_active_ = false;
    bool panel_ack_high_ = false;
    bool reset_held_ = false;
    bool reset_low_seen_ = false;
    bool adapter_error_ = false;
    bool cyclic_ready_suppressed_ = false;
    bool first_exchange_logged_ = false;
    bool selected_container_valid_ = false;
    std::array<uint8_t, 32> selected_container_sha256_{};
    int64_t cyclic_ready_deadline_ns_ = VirtualTimerList::kNoDeadline;
    VirtualTimerList::Entry* cyclic_ready_timer_ = nullptr;

    ReadyChangedFn ready_changed_ = nullptr;
    void* ready_changed_context_ = nullptr;
};

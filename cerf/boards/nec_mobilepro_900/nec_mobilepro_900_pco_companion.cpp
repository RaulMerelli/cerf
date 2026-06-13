#include "nec_mobilepro_900_pco_companion.h"

#include "../../core/cerf_emulator.h"
#include "../../socs/pxa255/pxa255_btuart.h"
#include "../board_detector.h"

REGISTER_SERVICE(NecMobilePro900PcoCompanion);

bool NecMobilePro900PcoCompanion::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardDetector>();
    return bd && bd->GetBoard() == Board::NecMobilePro900;
}

void NecMobilePro900PcoCompanion::OnReady() {
    /* The CE battery driver reads the main battery synchronously: pco.dll
       sub_1BC2368 writes 0x70 to BTUART THR and waits <=200ms for the reply on
       RX. Observe THR so we can answer it. */
    emu_.Get<Pxa255Btuart>().SetTxObserver([this](uint8_t b) { OnBtuartTx(b); });
}

void NecMobilePro900PcoCompanion::OnBtuartTx(uint8_t b) {
    /* 0x70 = PIC_MAIN_BAT_STATE_REQUEST. Reply with PIC_BATTERY_STATE (0x70)
       followed by the 16-bit value, high byte first — pco.dll's parser
       (sub_1BC28B4 states 2->3) reassembles (hi<<8)|lo and hands it to
       sub_1BC1E80, which caches it and signals the request's wait event. */
    if (b != 0x70u) return;
    const uint16_t raw = main_battery_raw_.load(std::memory_order_acquire);
    std::lock_guard<std::mutex> lk(report_mtx_);
    PushByte(0x70u);
    PushByte(static_cast<uint8_t>(raw >> 8));
    PushByte(static_cast<uint8_t>(raw & 0xFFu));
}

void NecMobilePro900PcoCompanion::PushByte(uint8_t b) {
    emu_.Get<Pxa255Btuart>().PushRx(b);
}

/* Keyboard and touch reports are streamed from separate pacer threads into one
   BTUART RX FIFO; report_mtx_ keeps each report's bytes contiguous so pco's
   byte-stream parser (sub_1BC28B4) never sees a touch report spliced into a
   keyboard report (which would corrupt both — e.g. a stuck key). */
void NecMobilePro900PcoCompanion::SendKeyboardMatrix(const uint8_t matrix[13]) {
    std::lock_guard<std::mutex> lk(report_mtx_);
    PushByte(0x13u);
    for (int i = 0; i < 13; ++i) PushByte(matrix[i]);
    PushByte(0x12u);
}

void NecMobilePro900PcoCompanion::SendTouch(uint16_t x, uint16_t y) {
    std::lock_guard<std::mutex> lk(report_mtx_);
    PushByte(0x04u);
    PushByte(static_cast<uint8_t>(x >> 8)); PushByte(static_cast<uint8_t>(x));
    PushByte(static_cast<uint8_t>(y >> 8)); PushByte(static_cast<uint8_t>(y));
}

void NecMobilePro900PcoCompanion::SendPenUp() {
    std::lock_guard<std::mutex> lk(report_mtx_);
    PushByte(0x05u);
}

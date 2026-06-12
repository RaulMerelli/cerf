#include "nec_mobilepro_900_pco_companion.h"

#include "../../core/cerf_emulator.h"
#include "../../socs/pxa255/pxa255_btuart.h"
#include "../board_detector.h"

REGISTER_SERVICE(NecMobilePro900PcoCompanion);

bool NecMobilePro900PcoCompanion::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardDetector>();
    return bd && bd->GetBoard() == Board::NecMobilePro900;
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

#pragma once

#include "../../core/service.h"

/* S3C2410A UM pp. 9-26/9-27: EINTMASK and EINTPEND serve "20 external
   interrupts (EINT[23:4])". UM p. 14-7: SRCPND EINT4_7 [4], EINT8_23 [5]. */
class S3C2410EintSink {
public:
    virtual ~S3C2410EintSink() = default;

    virtual void DriveEintPin(int eint, bool level) = 0;
};

class S3C2410EintSource : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;

    void SetSink(S3C2410EintSink* sink);

    void DriveEintPin(int eint, bool level);

private:
    S3C2410EintSink* sink_ = nullptr;
};

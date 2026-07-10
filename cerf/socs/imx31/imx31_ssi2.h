#pragma once

#include "imx31_ssi_impl.h"

/* MCIMX31RM Table 45-4: SSI2 at 0x50014000. Named (not anonymous) so
   Imx31AudioPlayer can read its live transmit format. */
class Imx31Ssi2 : public cerf_imx31_ssi_detail::Imx31SsiImpl<0x50014000u> {
public:
    using Imx31SsiImpl::Imx31SsiImpl;
};

#pragma once

#if defined(_MSC_VER) && _MSC_VER < 1600
typedef unsigned int uint32_t;
#else
#include <cstdint>
#endif

namespace CerfVirt {

/* Host->guest: the host publishes the desired guest resolution and bumps
   kRszWantGen last. The guest re-mode pump watches kRszWantGen. */
const uint32_t kRszWantW   = 0x00u;
const uint32_t kRszWantH   = 0x04u;
const uint32_t kRszWantBpp = 0x08u;
const uint32_t kRszWantGen = 0x0Cu;

/* Guest->host: after ChangeDisplaySettingsEx succeeds, the guest writes the
   mode it actually applied and bumps kRszAppliedGen last. The host reacts to
   the gen change by re-pointing the renderer/canvas at the new dimensions. */
const uint32_t kRszAppliedW   = 0x10u;
const uint32_t kRszAppliedH   = 0x14u;
const uint32_t kRszAppliedGen = 0x18u;

}  /* namespace CerfVirt */

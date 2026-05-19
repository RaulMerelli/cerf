#pragma once

#include <cstdint>

#include "cpu_state.h"

struct ShadowStackEntry {
    uint32_t guest_return_addr;
    void*    native_destination;
};

/* Compile-time enumeration of the five ARM exception types whose
   vector native PCs the JIT caches. ArmCpuRaise* helpers consult
   the cache via NativeAddr / SetNativeAddr; cache is reset by
   FlushNativeAddrCache when the translation cache flushes. */
enum class ExceptionVector : uint32_t {
    kUndef         = 0,
    kSwi           = 1,
    kAbortPrefetch = 2,
    kAbortData     = 3,
    kIrq           = 4,
    kCount         = 5,
};

constexpr uint32_t kFlagListByCondPair[8] = {
    /* EQ/NE   */ kFlagZ,
    /* CS/CC   */ kFlagC,
    /* MI/PL   */ kFlagN,
    /* VS/VC   */ kFlagV,
    /* HI/LS   */ kFlagZ | kFlagC,
    /* GE/LT   */ kFlagN | kFlagV,
    /* GT/LE   */ kFlagZ | kFlagN | kFlagV,
    /* AL/NV   */ kFlagsNone,
};

/* JIT optimization master switch. CERF assumes always-on; the
   conditional-optimization branches in JitOptimizeIR collapse to
   unconditional when this is true. */
constexpr bool kOptimizeJitCode = true;

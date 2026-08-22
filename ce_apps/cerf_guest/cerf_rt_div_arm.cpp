/* Local ARM RTABI division helpers, linked into cerf_guest ONLY. The SDK
   ordinal coredll.lib and every by-name coredll.lib carry import stubs for
   these names, so a local definition in any TU that links one of those libs
   trips LNK2005 - hence this dedicated file and the two names removed from
   coredll_byname.def.

   ARM IHI 0043 (RTABI): __rt_sdiv/__rt_udiv return the quotient in R0 and
   the remainder in R1. A 64-bit return lands in exactly R0:R1, so packing
   {quotient, remainder} into one 64-bit value serves callers that read R0
   alone and callers that read both. No CE coredll exports either name
   (verified: hmi_ktp400_mobile_v13 CE8 coredll.dll export table), so they
   must be local; division uses shift-subtract only - a C '/' or '%' here
   would re-emit a call to the helper itself. */

static unsigned int rt_udiv_core(unsigned int un, unsigned int ud,
                                 unsigned int* rem) {
    unsigned int q = 0, r = 0;
    for (int i = 31; i >= 0; --i) {
        r = (r << 1) | ((un >> i) & 1u);
        if (r >= ud) { r -= ud; q |= (1u << i); }
    }
    *rem = r;
    return q;
}

extern "C" ULONGLONG __rt_udiv(unsigned int un, unsigned int ud) {
    union { ULONGLONG s; unsigned int w[2]; } v;
    if (ud == 0u) { v.s = 0; return v.s; }
    unsigned int rem;
    v.w[0] = rt_udiv_core(un, ud, &rem);
    v.w[1] = rem;
    return v.s;
}

extern "C" LONGLONG __rt_sdiv(int num, int den) {
    union { LONGLONG s; unsigned int w[2]; } v;
    if (den == 0) { v.s = 0; return v.s; }
    unsigned int un = (unsigned int)num, ud = (unsigned int)den;
    const unsigned int qsign = (un ^ ud) >> 31;
    if (un & 0x80000000u) un = (~un) + 1u;
    if (ud & 0x80000000u) ud = (~ud) + 1u;
    unsigned int rem;
    unsigned int q = rt_udiv_core(un, ud, &rem);
    if (qsign) q = (~q) + 1u;
    if ((unsigned int)num & 0x80000000u) rem = (~rem) + 1u;
    v.w[0] = q; v.w[1] = rem;
    return v.s;
}

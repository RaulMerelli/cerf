#include <windows.h>
#include <stddef.h>

#if defined(MIPS)
#pragma function(memset, memcpy, __ll_lshift)
#else
#pragma function(memset, memcpy)
#endif

extern "C" void* __cdecl memset(void* dst, int c, size_t n) {
    unsigned char* p = (unsigned char*)dst;
    while (n--) *p++ = (unsigned char)c;
    return dst;
}

extern "C" void* __cdecl memcpy(void* dst, const void* src, size_t n) {
    unsigned char*       d = (unsigned char*)dst;
    const unsigned char* s = (const unsigned char*)src;
    while (n--) *d++ = *s++;
    return dst;
}

extern "C" void* __cdecl memmove(void* dst, const void* src, size_t n) {
    unsigned char*       d = (unsigned char*)dst;
    const unsigned char* s = (const unsigned char*)src;
    if (d == s || n == 0) return dst;
    if (d < s) {
        while (n--) *d++ = *s++;
    } else {
        d += n;
        s += n;
        while (n--) *--d = *--s;
    }
    return dst;
}

extern "C" int __cdecl _purecall(void) {
    return 0;
}

extern "C" __int64 __ll_div(__int64 num, __int64 den) {
    union { __int64 s; unsigned int w[2]; } n, d, q;
    n.s = num; d.s = den;
    if (d.w[0] == 0u && d.w[1] == 0u) return 0;

    int neg = 0;
    unsigned int nlo = n.w[0], nhi = n.w[1];
    unsigned int dlo = d.w[0], dhi = d.w[1];

    if (nhi & 0x80000000u) {
        nlo = (~nlo) + 1u;
        nhi = (~nhi) + (nlo == 0u ? 1u : 0u);
        neg ^= 1;
    }
    if (dhi & 0x80000000u) {
        dlo = (~dlo) + 1u;
        dhi = (~dhi) + (dlo == 0u ? 1u : 0u);
        neg ^= 1;
    }

    unsigned int qlo = 0u, qhi = 0u;
    unsigned int rlo = 0u, rhi = 0u;
    for (int i = 63; i >= 0; --i) {
        rhi = (rhi << 1) | (rlo >> 31);
        rlo <<= 1;
        rlo |= (i < 32) ? ((nlo >> i) & 1u) : ((nhi >> (i - 32)) & 1u);
        if (rhi > dhi || (rhi == dhi && rlo >= dlo)) {
            unsigned int borrow = (rlo < dlo) ? 1u : 0u;
            rlo = rlo - dlo;
            rhi = rhi - dhi - borrow;
            if (i < 32) qlo |= (1u << i);
            else        qhi |= (1u << (i - 32));
        }
    }

    if (neg) {
        qlo = (~qlo) + 1u;
        qhi = (~qhi) + (qlo == 0u ? 1u : 0u);
    }
    q.w[0] = qlo; q.w[1] = qhi;
    return q.s;
}

#if defined(MIPS)
extern "C" ULONGLONG NTAPI __ll_lshift(ULONGLONG Value, DWORD ShiftCount) {
    union { ULONGLONG q; unsigned int w[2]; } u;
    u.q = Value;
    unsigned int lo = u.w[0];
    unsigned int hi = u.w[1];
    unsigned int n  = (unsigned int)ShiftCount & 0x3Fu;
    if (n == 0u) return Value;
    if (n < 32u) {
        u.w[0] = lo << n;
        u.w[1] = (hi << n) | (lo >> (32u - n));
    } else {
        u.w[0] = 0u;
        u.w[1] = lo << (n - 32u);
    }
    return u.q;
}
#endif

void* __cdecl operator new(size_t n) {
    if (n == 0) n = 1;
    return LocalAlloc(LMEM_FIXED, n);
}

void* __cdecl operator new[](size_t n) {
    return operator new(n);
}

void __cdecl operator delete(void* p) {
    if (p) LocalFree((HLOCAL)p);
}

void __cdecl operator delete[](void* p) {
    operator delete(p);
}

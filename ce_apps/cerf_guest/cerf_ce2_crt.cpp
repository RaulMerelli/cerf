#include <windows.h>
#include <stddef.h>

#pragma function(memset, memcpy, __ll_lshift)

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
    if (den == 0) return 0;

    int negate = 0;
    unsigned __int64 n, d;

    if (num < 0) { n = (unsigned __int64)(-num); negate ^= 1; }
    else         { n = (unsigned __int64)num; }
    if (den < 0) { d = (unsigned __int64)(-den); negate ^= 1; }
    else         { d = (unsigned __int64)den; }

    unsigned __int64 q = 0;
    unsigned __int64 r = 0;
    for (int i = 63; i >= 0; --i) {
        r <<= 1;
        r |= (n >> i) & 1u;
        if (r >= d) {
            r -= d;
            q |= ((unsigned __int64)1) << i;
        }
    }
    return negate ? -(__int64)q : (__int64)q;
}

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

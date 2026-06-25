#pragma once

#define DDI 1

#include <windows.h>

/* The CE SDK defines REG_TYPE only inside its MIPS register-context block;
   pkfuncs.h uses it as a type, so provide it where the SDK does not (e.g. ARM). */
#ifndef REG_TYPE
typedef DWORD REG_TYPE;
#endif

#ifndef _BLENDFUNCTION_DEFINED
#define _BLENDFUNCTION_DEFINED
typedef struct _BLENDFUNCTION {
    BYTE BlendOp;
    BYTE BlendFlags;
    BYTE SourceConstantAlpha;
    BYTE AlphaFormat;
} BLENDFUNCTION, *PBLENDFUNCTION;
#endif

#ifndef AC_SRC_OVER
#define AC_SRC_OVER   0x00
#define AC_SRC_ALPHA  0x01
#endif

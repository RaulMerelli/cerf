#pragma once

#define DDI 1

#include <windows.h>

typedef DWORD REG_TYPE;

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

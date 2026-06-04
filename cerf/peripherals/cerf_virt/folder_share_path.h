#pragma once

#define NOMINMAX
#include <windows.h>

#include "../../core/service.h"

#include <cstdint>
#include <string>

/* Path resolution + CE<->Win32 conversions shared by the folder-share file and
   directory op services. ToWin32Path joins the guest's fully-qualified CE name
   onto the host share root and canonicalises it, rejecting any result that
   escapes the root (the guest's only sandbox boundary). */
class FolderSharePath : public Service {
public:
    using Service::Service;

    /* ce_name: guest CE wide name (uint16_t code units, NOT NUL-terminated),
       ce_len_bytes excludes the terminator. Returns a kError* code (0 = ok);
       on success `out` is a host path guaranteed under the share root. */
    uint16_t ToWin32Path(const uint16_t* ce_name, uint16_t ce_len_bytes,
                         std::wstring& out) const;

    static uint32_t FiletimeToLong(const FILETIME& ft);
    static bool     LongToFiletime(uint32_t dos_datetime, FILETIME& out);
    static uint16_t CeFileAttributes(DWORD win32_attrs);
    static uint16_t ErrorFromLastError();   /* GetLastError() -> kError* */
};

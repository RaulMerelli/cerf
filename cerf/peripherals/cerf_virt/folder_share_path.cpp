#include "folder_share_path.h"

#include "cerf_virt_folder_share_regs.h"
#include "../../core/cerf_emulator.h"
#include "../../core/folder_share_config.h"

#include <cwchar>

REGISTER_SERVICE(FolderSharePath);

uint16_t FolderSharePath::ToWin32Path(const uint16_t* ce_name,
                                      uint16_t ce_len_bytes,
                                      std::wstring& out) const {

    if (ce_len_bytes > CerfVirt::kMaxLfn * sizeof(uint16_t))
        ce_len_bytes = (uint16_t)(CerfVirt::kMaxLfn * sizeof(uint16_t));

    std::wstring root = emu_.Get<FolderShareConfig>().HostRoot();
    wchar_t canon_root[MAX_PATH];
    const DWORD rn = GetFullPathNameW(root.c_str(), MAX_PATH, canon_root, nullptr);
    if (rn == 0 || rn >= MAX_PATH) return CerfVirt::kErrorPathNotFound;
    root.assign(canon_root);
    while (!root.empty() && (root.back() == L'\\' || root.back() == L'/'))
        root.pop_back();
    if (root.empty()) return CerfVirt::kErrorPathNotFound;

    std::wstring joined = root;
    joined.append(reinterpret_cast<const wchar_t*>(ce_name),
                  ce_len_bytes / sizeof(uint16_t));

    wchar_t full[MAX_PATH];
    const DWORD n = GetFullPathNameW(joined.c_str(), MAX_PATH, full, nullptr);
    if (n == 0 || n >= MAX_PATH) return CerfVirt::kErrorInvalidName;

    const size_t rlen = root.size();
    if (_wcsnicmp(full, root.c_str(), rlen) != 0) return CerfVirt::kErrorInvalidName;
    if (full[rlen] != L'\\' && full[rlen] != L'/' && full[rlen] != L'\0')
        return CerfVirt::kErrorInvalidName;

    out.assign(full);
    return CerfVirt::kErrorNoError;
}

uint32_t FolderSharePath::FiletimeToLong(const FILETIME& ft) {
    FILETIME local;
    WORD dos_date, dos_time;
    if (FileTimeToLocalFileTime(&ft, &local) &&
        FileTimeToDosDateTime(&local, &dos_date, &dos_time)) {
        return MAKELONG(dos_time, dos_date);
    }
    return 0;
}

bool FolderSharePath::LongToFiletime(uint32_t dos_datetime, FILETIME& out) {
    FILETIME local;
    if (!DosDateTimeToFileTime(HIWORD(dos_datetime), LOWORD(dos_datetime), &local))
        return false;
    return LocalFileTimeToFileTime(&local, &out) != 0;
}

uint16_t FolderSharePath::CeFileAttributes(DWORD win32_attrs) {
    return (uint16_t)(win32_attrs &
        (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_HIDDEN |
         FILE_ATTRIBUTE_ARCHIVE | FILE_ATTRIBUTE_DIRECTORY));
}


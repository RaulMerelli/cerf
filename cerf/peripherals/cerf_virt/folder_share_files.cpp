#include "folder_share_files.h"

#include "folder_share_path.h"
#include "folder_share_mmu.h"
#include "cerf_virt_folder_share_regs.h"
#include "../../core/cerf_emulator.h"
#include "../../core/device_config.h"
#include "../../core/folder_share_config.h"

#include <cstring>
#include <vector>

using namespace CerfVirt;

REGISTER_SERVICE(FolderShareFiles);

namespace {

uint16_t ErrFromLast() {
    switch (GetLastError()) {
        case ERROR_HANDLE_DISK_FULL:        return kErrorDiskFull;
        case ERROR_PATH_NOT_FOUND:          return kErrorPathNotFound;
        case ERROR_FILE_NOT_FOUND:          return kErrorFileNotFound;
        case ERROR_WRITE_PROTECT:           return kErrorWriteProtect;
        case ERROR_FILE_EXISTS:             return kErrorFileExists;
        case ERROR_ALREADY_EXISTS:          return kErrorFileExists;
        case ERROR_TOO_MANY_OPEN_FILES:     return kErrorTooManyOpenFiles;
        case ERROR_NO_MORE_FILES:           return kErrorNoMoreFiles;
        case ERROR_INVALID_NAME:            return kErrorInvalidName;
        case ERROR_ACCESS_DENIED:           return kErrorAccessDenied;
        case ERROR_LOCK_VIOLATION:          return kErrorLockViolation;
        case ERROR_SHARING_BUFFER_EXCEEDED: return kErrorSharingBufferExceeded;
        case ERROR_SHARING_VIOLATION:       return kErrorSharingViolation;
        default:                            return kErrorGeneralFailure;
    }
}

bool DesiredAccess(uint16_t mode, DWORD* out) {
    switch (mode & 0xf) {
        case kOpenAccessReadOnly:  *out = GENERIC_READ;                 return true;
        case kOpenAccessWriteOnly: *out = GENERIC_WRITE;                return true;
        case kOpenAccessReadWrite: *out = GENERIC_READ | GENERIC_WRITE; return true;
        default:                   *out = 0;                           return false;
    }
}

bool ShareModeOf(uint16_t mode, DWORD* out) {
    switch (mode & 0xf0) {
        case kOpenShareDenyReadWrite: *out = 0;                                 return true;
        case kOpenShareDenyWrite:     *out = FILE_SHARE_READ;                   return true;
        case kOpenShareDenyRead:      *out = FILE_SHARE_WRITE;                  return true;
        case kOpenShareDenyNone:      *out = FILE_SHARE_READ | FILE_SHARE_WRITE; return true;
        default:                      *out = 0;                                 return false;
    }
}

}  /* namespace */

bool FolderShareFiles::ShouldRegister() {
    return emu_.Get<DeviceConfig>().guest_additions;
}

bool FolderShareFiles::Owns(uint32_t code) {
    switch (code) {
        case kServerGetDriveConfig: case kServerCreate: case kServerOpen:
        case kServerRead: case kServerWrite: case kServerSetEOF:
        case kServerClose: case kServerGetFCBInfo: case kServerGetSpace:
        case kServerGetMaxIOSize:
            return true;
        default:
            return false;
    }
}

uint32_t FolderShareFiles::Run(uint32_t code, ServerPB& pb) {
    switch (code) {
        case kServerGetMaxIOSize:   return kFolderShareMaxReadWriteSize;
        case kServerGetDriveConfig: return GetDriveConfig(pb);
        case kServerCreate:         return Create(pb);
        case kServerOpen:           return Open(pb);
        case kServerRead:           return Read(pb);
        case kServerWrite:          return Write(pb);
        case kServerSetEOF:         return SetEof(pb);
        case kServerClose:          return Close(pb);
        case kServerGetFCBInfo:     return GetFcbInfo(pb);
        case kServerGetSpace:       return GetSpace(pb);
        default:                    return kErrorInvalidFunction;
    }
}

uint16_t FolderShareFiles::AllocSlot() {
    for (uint16_t i = 0; i < kMaxFd; ++i)
        if (files_[i].h == INVALID_HANDLE_VALUE) return i;
    return 0xFFFF;
}

uint16_t FolderShareFiles::GetDriveConfig(ServerPB&) {
    return emu_.Get<FolderShareConfig>().Enabled() ? kErrorNoError
                                                   : kErrorGeneralFailure;
}

uint16_t FolderShareFiles::Create(ServerPB& pb) {
    std::wstring path;
    uint16_t e = emu_.Get<FolderSharePath>().ToWin32Path(
        pb.fLfn.fName, pb.fLfn.fNameLength, path);
    if (e != kErrorNoError) return e;

    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, CREATE_NEW, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return ErrFromLast();
    CloseHandle(h);
    return kErrorNoError;
}

uint16_t FolderShareFiles::Open(ServerPB& pb) {
    const uint16_t slot = AllocSlot();
    if (slot == 0xFFFF) return kErrorTooManyOpenFiles;

    std::wstring path;
    uint16_t e = emu_.Get<FolderSharePath>().ToWin32Path(
        pb.fLfn.fName, pb.fLfn.fNameLength, path);
    if (e != kErrorNoError) return e;

    DWORD access, share;
    if (!DesiredAccess(pb.fOpenMode, &access) || !ShareModeOf(pb.fOpenMode, &share))
        return kErrorInvalidFunction;

    /* A directory handle (which the shell opens for the folder view + change
       notifications) needs FILE_FLAG_BACKUP_SEMANTICS, and a trailing separator
       makes CreateFileW reject the path; without this an AFS volume can't be
       opened as a handle and the kernel's change-notify setup derefs null. */
    while (path.size() > 3 && (path.back() == L'\\' || path.back() == L'/'))
        path.pop_back();
    HANDLE h = CreateFileW(path.c_str(), access, share, nullptr,
                           OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (h == INVALID_HANDLE_VALUE) return ErrFromLast();

    files_[slot].h = h;
    files_[slot].open_mode = pb.fOpenMode;
    files_[slot].name.assign(reinterpret_cast<const wchar_t*>(pb.fLfn.fName),
                             pb.fLfn.fNameLength / sizeof(uint16_t));
    pb.fHandle = slot;
    return kErrorNoError;
}

uint16_t FolderShareFiles::Read(ServerPB& pb) {
    if (pb.fHandle >= kMaxFd) return kErrorInvalidHandle;
    HANDLE h = files_[pb.fHandle].h;
    if (h == INVALID_HANDLE_VALUE) return kErrorInvalidHandle;

    uint32_t want = pb.fSize;
    if (want > kFolderShareMaxReadWriteSize) want = kFolderShareMaxReadWriteSize;

    LARGE_INTEGER pos; pos.QuadPart = pb.fPosition;
    if (!SetFilePointerEx(h, pos, nullptr, FILE_BEGIN)) return ErrFromLast();

    std::vector<uint8_t> buf(want ? want : 1);
    DWORD got = 0;
    if (!ReadFile(h, buf.data(), want, &got, nullptr)) return ErrFromLast();
    if (got && !emu_.Get<FolderShareMmu>().WriteBlob(pb.fDTAPtr, buf.data(), got))
        return kErrorReadFault;
    pb.fSize = got;
    return kErrorNoError;
}

uint16_t FolderShareFiles::Write(ServerPB& pb) {
    if (pb.fHandle >= kMaxFd) return kErrorInvalidHandle;
    HANDLE h = files_[pb.fHandle].h;
    if (h == INVALID_HANDLE_VALUE) return kErrorInvalidHandle;

    const uint32_t want = pb.fSize;
    if (want > kFolderShareMaxReadWriteSize) return kErrorWriteFault;

    std::vector<uint8_t> buf(want ? want : 1);
    if (want && !emu_.Get<FolderShareMmu>().ReadBlob(pb.fDTAPtr, buf.data(), want))
        return kErrorWriteFault;

    LARGE_INTEGER pos; pos.QuadPart = pb.fPosition;
    if (!SetFilePointerEx(h, pos, nullptr, FILE_BEGIN)) return ErrFromLast();

    DWORD put = 0;
    if (!WriteFile(h, buf.data(), want, &put, nullptr)) return ErrFromLast();
    pb.fSize = put;
    return kErrorNoError;
}

uint16_t FolderShareFiles::SetEof(ServerPB& pb) {
    if (pb.fHandle >= kMaxFd) return kErrorInvalidHandle;
    HANDLE h = files_[pb.fHandle].h;
    if (h == INVALID_HANDLE_VALUE) return kErrorInvalidHandle;

    LARGE_INTEGER pos; pos.QuadPart = pb.fPosition;
    if (!SetFilePointerEx(h, pos, nullptr, FILE_BEGIN)) return ErrFromLast();
    if (!SetEndOfFile(h)) return ErrFromLast();
    return kErrorNoError;
}

uint16_t FolderShareFiles::Close(ServerPB& pb) {
    if (pb.fHandle >= kMaxFd) return kErrorInvalidHandle;
    Slot& s = files_[pb.fHandle];
    if (s.h == INVALID_HANDLE_VALUE) return kErrorInvalidHandle;
    BOOL ok = CloseHandle(s.h);
    s.h = INVALID_HANDLE_VALUE;
    s.name.clear();
    return ok ? kErrorNoError : ErrFromLast();
}

uint16_t FolderShareFiles::GetFcbInfo(ServerPB& pb) {
    if (pb.fHandle >= kMaxFd) return kErrorInvalidHandle;
    Slot& s = files_[pb.fHandle];
    if (s.h == INVALID_HANDLE_VALUE) return kErrorInvalidHandle;

    LARGE_INTEGER cur, zero; zero.QuadPart = 0;
    if (!SetFilePointerEx(s.h, zero, &cur, FILE_CURRENT)) return ErrFromLast();
    BY_HANDLE_FILE_INFORMATION fi;
    if (!GetFileInformationByHandle(s.h, &fi)) return ErrFromLast();

    pb.fPosition = cur.HighPart ? 0xffffffff : cur.LowPart;
    pb.fSize = fi.nFileSizeHigh ? 0xffffffff : fi.nFileSizeLow;
    pb.fFileAttributes = FolderSharePath::CeFileAttributes(fi.dwFileAttributes);
    pb.fOpenMode = s.open_mode;

    size_t n = s.name.size();
    if (n > kMaxLfn) n = kMaxLfn;
    pb.fLfn.fNameLength = (uint16_t)(n * sizeof(uint16_t));
    std::memcpy(pb.fLfn.fName, s.name.data(), n * sizeof(uint16_t));
    pb.fLfn.fName[n] = 0;
    return kErrorNoError;
}

uint16_t FolderShareFiles::GetSpace(ServerPB& pb) {
    std::wstring root = emu_.Get<FolderShareConfig>().HostRoot();
    if (root.empty()) return kErrorPathNotFound;

    ULARGE_INTEGER freeb, totalb;
    if (!GetDiskFreeSpaceExW(root.c_str(), &freeb, &totalb, nullptr))
        return ErrFromLast();
    if (freeb.HighPart)  freeb.LowPart  = 0xffffffff;
    if (totalb.HighPart) totalb.LowPart = 0xffffffff;
    pb.fSize     = freeb.LowPart  / 32768;
    pb.fPosition = totalb.LowPart / 32768;
    return kErrorNoError;
}

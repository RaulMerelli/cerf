#include "folder_share_dir.h"

#include "folder_share_path.h"
#include "cerf_virt_folder_share_regs.h"
#include "../../core/cerf_emulator.h"
#include "../../core/device_config.h"

#include <cstring>
#include <cwchar>

using namespace CerfVirt;

REGISTER_SERVICE(FolderShareDir);

bool FolderShareDir::ShouldRegister() {
    return emu_.Get<DeviceConfig>().guest_additions;
}

void FolderShareDir::OnReady() {
    for (auto& f : finds_) f = INVALID_HANDLE_VALUE;
}

bool FolderShareDir::Owns(uint32_t code) {
    switch (code) {
        case kServerMkDir: case kServerRmDir: case kServerSetAttributes:
        case kServerRename: case kServerDelete: case kServerGetInfo:
            return true;
        default:
            return false;
    }
}

uint32_t FolderShareDir::Run(uint32_t code, ServerPB& pb) {
    switch (code) {
        case kServerMkDir:          return MkDir(pb);
        case kServerRmDir:          return RmDir(pb);
        case kServerSetAttributes:  return SetAttributes(pb);
        case kServerRename:         return Rename(pb);
        case kServerDelete:         return Delete(pb);
        case kServerGetInfo:        return GetInfo(pb);
        default:                    return kErrorInvalidFunction;
    }
}

uint16_t FolderShareDir::MkDir(ServerPB& pb) {
    std::wstring p;
    uint16_t e = emu_.Get<FolderSharePath>().ToWin32Path(
        pb.fLfn.fName, pb.fLfn.fNameLength, p);
    if (e != kErrorNoError) return e;
    return CreateDirectoryW(p.c_str(), nullptr)
               ? kErrorNoError : FolderSharePath::ErrorFromLastError();
}

uint16_t FolderShareDir::RmDir(ServerPB& pb) {
    std::wstring p;
    uint16_t e = emu_.Get<FolderSharePath>().ToWin32Path(
        pb.fLfn.fName, pb.fLfn.fNameLength, p);
    if (e != kErrorNoError) return e;
    return RemoveDirectoryW(p.c_str())
               ? kErrorNoError : FolderSharePath::ErrorFromLastError();
}

uint16_t FolderShareDir::Delete(ServerPB& pb) {
    std::wstring p;
    uint16_t e = emu_.Get<FolderSharePath>().ToWin32Path(
        pb.fLfn.fName, pb.fLfn.fNameLength, p);
    if (e != kErrorNoError) return e;
    return DeleteFileW(p.c_str())
               ? kErrorNoError : FolderSharePath::ErrorFromLastError();
}

uint16_t FolderShareDir::Rename(ServerPB& pb) {
    auto& path = emu_.Get<FolderSharePath>();
    std::wstring oldn, newn;
    uint16_t e = path.ToWin32Path(pb.fLfn.fName, pb.fLfn.fNameLength, oldn);
    if (e != kErrorNoError) return e;
    e = path.ToWin32Path(pb.fLfn.fName2, pb.fLfn.fName2Length, newn);
    if (e != kErrorNoError) return e;
    /* MOVEFILE_COPY_ALLOWED: the share root may span more than one host volume. */
    return MoveFileExW(oldn.c_str(), newn.c_str(), MOVEFILE_COPY_ALLOWED)
               ? kErrorNoError : FolderSharePath::ErrorFromLastError();
}

uint16_t FolderShareDir::SetAttributes(ServerPB& pb) {
    std::wstring file;
    uint16_t e = emu_.Get<FolderSharePath>().ToWin32Path(
        pb.fLfn.fName, pb.fLfn.fNameLength, file);
    if (e != kErrorNoError) return e;

    if (!SetFileAttributesW(file.c_str(), pb.fFileAttributes))
        return FolderSharePath::ErrorFromLastError();

    if (pb.fFileTimeDate) {
        FILETIME ft;
        if (!FolderSharePath::LongToFiletime(pb.fFileTimeDate, ft))
            return kErrorGeneralFailure;
        HANDLE h = CreateFileW(file.c_str(), FILE_WRITE_ATTRIBUTES,
            FILE_SHARE_DELETE | FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_NO_RECALL, nullptr);
        if (h == INVALID_HANDLE_VALUE) return FolderSharePath::ErrorFromLastError();
        BOOL ok = SetFileTime(h, nullptr, nullptr, &ft);
        CloseHandle(h);
        if (!ok) return FolderSharePath::ErrorFromLastError();
    }
    return kErrorNoError;
}

void FolderShareDir::UpdateFromFind(ServerPB& pb, const WIN32_FIND_DATAW& fd,
                                    bool writeback_name) {
    pb.fFileCreateTimeDate = FolderSharePath::FiletimeToLong(fd.ftCreationTime);
    pb.fFileTimeDate       = FolderSharePath::FiletimeToLong(fd.ftLastWriteTime);
    pb.fFileAttributes     = FolderSharePath::CeFileAttributes(fd.dwFileAttributes);
    pb.fSize = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? 0
             : (fd.nFileSizeHigh ? 0xffffffff : fd.nFileSizeLow);
    if (writeback_name) {
        size_t n = wcslen(fd.cFileName);
        if (n > kMaxLfn) n = kMaxLfn;
        pb.fLfn.fNameLength = (uint16_t)(n * sizeof(uint16_t));
        std::memcpy(pb.fLfn.fName, fd.cFileName, n * sizeof(uint16_t));
        pb.fLfn.fName[n] = 0;
    }
}

uint16_t FolderShareDir::GetInfo(ServerPB& pb) {
    auto& path = emu_.Get<FolderSharePath>();
    const int16_t idx = pb.fIndex;

    if (idx == -1) {
        /* One-shot existence/info lookup; never part of a Find enumeration. */
        if (pb.fFindTransactionID != 0xffffffff) return kErrorInvalidFunction;
        std::wstring file;
        uint16_t e = path.ToWin32Path(pb.fLfn.fName, pb.fLfn.fNameLength, file);
        if (e != kErrorNoError) return e;

        WIN32_FILE_ATTRIBUTE_DATA fad;
        if (!GetFileAttributesExW(file.c_str(), GetFileExInfoStandard, &fad))
            return FolderSharePath::ErrorFromLastError();
        pb.fFileCreateTimeDate = FolderSharePath::FiletimeToLong(fad.ftCreationTime);
        pb.fFileTimeDate       = FolderSharePath::FiletimeToLong(fad.ftLastWriteTime);
        pb.fFileAttributes     = FolderSharePath::CeFileAttributes(fad.dwFileAttributes);
        pb.fSize = (fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? 0
                 : (fad.nFileSizeHigh ? 0xffffffff : fad.nFileSizeLow);
        return kErrorNoError;
    }

    if (idx == 0) {
        /* FindFirst with a wildcard in fName. */
        const uint32_t tid = pb.fFindTransactionID;
        if (tid != 0xffffffff && tid >= kMaxFc) return kErrorGeneralFailure;
        std::wstring spec;
        uint16_t e = path.ToWin32Path(pb.fLfn.fName, pb.fLfn.fNameLength, spec);
        if (e != kErrorNoError) return e;

        WIN32_FIND_DATAW fd;
        HANDLE hf = FindFirstFileW(spec.c_str(), &fd);
        if (hf == INVALID_HANDLE_VALUE) return FolderSharePath::ErrorFromLastError();
        while (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) {
            if (!FindNextFileW(hf, &fd)) {
                uint16_t er = FolderSharePath::ErrorFromLastError();
                FindClose(hf);
                return er;
            }
        }
        if (tid == 0xffffffff) {
            FindClose(hf);
        } else {
            if (finds_[tid] != INVALID_HANDLE_VALUE) FindClose(finds_[tid]);
            finds_[tid] = hf;
        }
        UpdateFromFind(pb, fd, tid != 0xffffffff);
        return kErrorNoError;
    }

    /* FindNext on an in-progress enumeration. */
    const uint32_t tid = pb.fFindTransactionID;
    if (tid >= kMaxFc) return kErrorGeneralFailure;
    HANDLE hf = finds_[tid];
    if (hf == INVALID_HANDLE_VALUE) return kErrorGeneralFailure;

    WIN32_FIND_DATAW fd;
    do {
        if (!FindNextFileW(hf, &fd)) {
            uint16_t er = FolderSharePath::ErrorFromLastError();
            FindClose(hf);
            finds_[tid] = INVALID_HANDLE_VALUE;
            return er;
        }
    } while (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L".."));
    UpdateFromFind(pb, fd, true);
    return kErrorNoError;
}

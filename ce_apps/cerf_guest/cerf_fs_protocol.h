#pragma once

/* Guest ABI for the folder-share channel. MUST stay byte-identical to the host
   side: the host validates fStructureSize == sizeof at runtime, so a layout
   change on either side alone fails every mount. */

#include <windows.h>

/* Channel physical base (host kFolderShareBase). The host reads the ServerPB by
   VA through the live MMU, so guest buffers are ordinary VirtualAlloc'd VAs. */
#define CERF_FS_CHANNEL_PA   0xD0005000u

/* Largest single read/write the host services in one ServerPB op. */
#define CERF_FS_MAX_IO       (64u * 1024u)
#define CERF_FS_MAX_LFN      255u   /* wide chars in a long file name */

/* ServerPB op codes (host kServer* in the regs header). */
#define CERF_FS_OP_POLL          0x00u
#define CERF_FS_OP_DRIVE_CONFIG  0x04u
#define CERF_FS_OP_CREATE        0x05u
#define CERF_FS_OP_OPEN          0x06u
#define CERF_FS_OP_READ          0x07u
#define CERF_FS_OP_WRITE         0x08u
#define CERF_FS_OP_SET_EOF       0x09u
#define CERF_FS_OP_CLOSE         0x0Au
#define CERF_FS_OP_GET_SPACE     0x0Bu
#define CERF_FS_OP_MKDIR         0x0Cu
#define CERF_FS_OP_RMDIR         0x0Du
#define CERF_FS_OP_SET_ATTRS     0x0Eu
#define CERF_FS_OP_RENAME        0x0Fu
#define CERF_FS_OP_DELETE        0x10u
#define CERF_FS_OP_GET_INFO      0x11u
#define CERF_FS_OP_GET_FCB_INFO  0x13u
#define CERF_FS_OP_GET_MAX_IO    0x15u

/* Result codes (host kError*). 0 == success. */
#define CERF_FS_OK               0x00u
#define CERF_FS_E_INVALID_FUNC   0x01u
#define CERF_FS_E_FILE_NOT_FOUND 0x02u
#define CERF_FS_E_PATH_NOT_FOUND 0x03u
#define CERF_FS_E_TOO_MANY_FILES 0x04u
#define CERF_FS_E_ACCESS_DENIED  0x05u
#define CERF_FS_E_INVALID_HANDLE 0x06u
#define CERF_FS_E_NO_MORE_FILES  0x12u
#define CERF_FS_E_GENERAL        0x1Fu

/* fOpenMode access (low nibble) and share (high nibble) bits. */
#define CERF_FS_ACCESS_READ      0x00u
#define CERF_FS_ACCESS_WRITE     0x01u
#define CERF_FS_ACCESS_RW        0x02u
#define CERF_FS_SHARE_DENY_RW    0x10u
#define CERF_FS_SHARE_DENY_WRITE 0x20u
#define CERF_FS_SHARE_DENY_READ  0x30u
#define CERF_FS_SHARE_DENY_NONE  0x40u

#pragma pack(push, 4)

/* Per-request parameter block in guest RAM; the host reads/writes it by VA. */
typedef struct CerfFsServerPB {
    unsigned short fStructureSize;          /* == sizeof(CerfFsServerPB) (1068) */
    unsigned short fResult;
    unsigned long  fFindTransactionID;
    short          fIndex;
    unsigned short fHandle;
    unsigned long  fFileTimeDate;
    unsigned long  fSize;
    unsigned long  fPosition;
    unsigned long  fDTAPtr;                 /* guest VA of the I/O buffer */
    unsigned short fFileAttributes;
    unsigned short fOpenMode;
    unsigned char  fWildCard;
    struct {
        unsigned short fNameLength;         /* bytes, excludes terminator */
        unsigned short fName[CERF_FS_MAX_LFN + 1];
        unsigned short fName2Length;
        unsigned short fName2[CERF_FS_MAX_LFN + 1];
    } u;
    unsigned long  fFileCreateTimeDate;
} CerfFsServerPB;

/* Memory-mapped channel registers (host serves these). */
typedef struct CerfFsChannel {
    volatile unsigned long  ServerPB;       /* 0x00  guest writes the ServerPB VA */
    volatile unsigned long  Code;           /* 0x04  guest writes the op -> runs it */
    volatile unsigned long  IOPending;      /* 0x08  host: nonzero while in flight */
    volatile unsigned long  Result;         /* 0x0C  host: op result */
    volatile unsigned long  Enabled;        /* 0x10  host: sharing on */
    volatile unsigned long  Generation;     /* 0x14  host: bumps on config change */
    volatile unsigned long  _pad18;
    volatile unsigned long  _pad1c;
    volatile unsigned short MountPoint[64]; /* 0x20  host: mount-point wide string */
} CerfFsChannel;

#pragma pack(pop)

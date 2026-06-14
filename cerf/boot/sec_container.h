#pragma once

#include <cstddef>
#include <cstdint>

class MappedFile;

/* Header of a Ford `.sec` update package (little-endian, first 0x40 bytes).
   Field offsets reverse-engineered + validated against the device `.sec`. */
struct SecHeader {
    uint32_t magic;         /* 0x00: 0x400D400D                               */
    uint32_t pkcs7_off;     /* 0x0C: offset of the PKCS#7 catalog (0x80)      */
    uint32_t file_size;     /* 0x18: total container size                     */
    uint32_t payload_off;   /* 0x24: offset of the chunked payload (0xEE54)   */
    uint32_t chunk_stride;  /* 0x28: bytes per chunk incl. header (0x800040)  */
    uint32_t chunk_count;   /* 0x2C: number of chunks (251)                   */
};

/* Reads the device's NAND flash, de-chunked from a `.sec`. The caller owns the
   MappedFile passed to each read (the 1.96 GiB file is never mapped whole). */
class SecContainer {
public:
    /* Parse + validate `mf`; returns false and stays invalid if `mf` isn't a `.sec`. */
    bool Open(MappedFile& mf);

    bool             IsValid() const { return valid_; }
    const SecHeader& Header()  const { return hdr_; }

    /* Total de-chunked flash size in bytes (chunk_count * data-per-chunk). */
    uint64_t FlashSize(const MappedFile& mf) const;

    /* Byte offset within the `.sec` file holding the flash byte at `flash_off`. */
    uint64_t FlashToFile(uint64_t flash_off) const;

    /* Copy `len` flash bytes at `flash_off` into `dst`, walking chunk
       boundaries (a single MappedFile read never crosses a 0x40 chunk header).
       Returns bytes copied (< len at end of flash). */
    size_t ReadFlash(MappedFile& mf, uint64_t flash_off, void* dst, size_t len) const;

private:
    SecHeader hdr_   {};
    bool      valid_ = false;
};

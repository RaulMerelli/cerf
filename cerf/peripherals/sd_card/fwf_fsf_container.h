#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace fwf_fsf {

/* One file the Siemens FSF volume carries, as a view into the FWF stream. */
struct FsfEntry {
    std::string dir;  /* directory below the volume root, "" for the root */
    std::string name; /* leaf name, e.g. "mshtml.dll" */
    std::vector<uint8_t> data;
};

/* Decodes the FSF volume the KTP_4_Mobile FWF stores as two consecutive
   InPlaceBlobStreamed payloads.  Returns an empty vector unless the record
   walk consumes the volume exactly. */
std::vector<FsfEntry> ParseFsfVolume(const std::vector<uint8_t>& fwf_stream);

/* The FAT32 primitives the seeding needs from the volume that owns
   them, so the record materialization does not duplicate the layout code. */
struct FatSink {
    uint32_t cluster_bytes = 0;
    /* Returns a free cluster already marked end-of-chain, or 0. */
    std::function<uint32_t()> alloc_cluster;
    std::function<void(uint32_t, uint32_t)> link_fat;
    std::function<uint8_t*(uint32_t)> cluster_ptr;             /* null on failure */
    std::function<void(uint32_t, uint32_t, uint32_t)> persist; /* cluster, off, len */
};

/* Writes every entry under the volume whose root directory starts at
   root_clus, creating the directories the records name.  Returns the number
   of files written. */
uint32_t SeedFsfVolume(const std::vector<FsfEntry>& entries, uint32_t root_clus, const FatSink& fat);

} /* namespace fwf_fsf */

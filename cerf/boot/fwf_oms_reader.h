#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace cerf::fwf_oms {

/* One blob-valued attribute of the serialized object tree, as a span over the
   caller's buffer. */
struct Blob {
    uint32_t attr_id = 0;
    std::string name; /* value of attribute 233 in scope, e.g. "InPlaceBlobStreamed" */
    size_t off = 0;
    size_t size = 0;
};

/* Walks a Siemens `.fwf`, which is an OMS+ object tree serialized to a file,
   and returns every blob attribute in stream order.  Returns an empty vector
   when `src` does not open as an OMS+ stream. */
std::vector<Blob> WalkBlobs(const uint8_t* src, size_t size);

/* True when `src` opens as an OMS+ stream, i.e. the stream-start byte followed
   by the root object. */
bool IsOmsStream(const uint8_t* src, size_t size);

/* Assembles the OS image the container streams: every deflate-compressed blob,
   inflated, in stream order.  Returns false when the file is not an OMS+
   stream or carries no streamed OS image. */
bool AssembleOsImage(const uint8_t* src, size_t size, std::vector<uint8_t>& out, size_t* slice_count = nullptr);

/* Returns the serialized Firmware object carried by the PersistentStream blob. */
bool ExtractPersistentStream(const uint8_t* src, size_t size, std::vector<uint8_t>& out);

/* Returns the installed-firmware description: the Firmware root's scalar
   metadata and Version child, without image/update containers. */
bool ExtractInstalledFirmwareSummary(const uint8_t* src, size_t size, std::vector<uint8_t>& out);

/* Assembles the FSF flash volume the container carries: the blob holding the
   FSF magic plus the raw blobs that continue it. */
bool AssembleFsfVolume(const uint8_t* src, size_t size, std::vector<uint8_t>& out);

} // namespace cerf::fwf_oms

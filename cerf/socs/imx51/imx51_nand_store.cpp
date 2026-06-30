#include "imx51_nand_store.h"
#include "imx51_nand_layout.h"

#include "../../boards/board_context.h"
#include "../../boot/sec_flash.h"
#include "../../core/cerf_emulator.h"
#include "../../core/cerf_paths.h"
#include "../../core/device_config.h"
#include "../../core/log.h"
#include "../../host/host_widget_registry.h"

#include <array>
#include <cstring>

REGISTER_SERVICE(Imx51NandStore);

std::string Imx51NandStore::ImagePath() const {
    return GetDeviceDir(emu_.Get<DeviceConfig>().device_name) + "nand.img";
}

static bool FileNonEmpty(const std::string& path) {
    WIN32_FILE_ATTRIBUTE_DATA fad{};
    return GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &fad) &&
           (((uint64_t(fad.nFileSizeHigh) << 32) | fad.nFileSizeLow) > 0);
}

bool Imx51NandStore::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    if (!bd || bd->GetSoc() != SocFamily::iMX51) return false;
    auto* sf = emu_.TryGet<SecFlash>();
    if (sf && sf->IsPresent()) return true;   /* can seed/flash from the `.sec` */
    return FileNonEmpty(ImagePath());         /* else: an already-flashed nand.img */
}

void Imx51NandStore::OnReady() {
    device_pages_ = kDeviceBytes / kMainBytes;

    const std::string path = ImagePath();
    const bool existed = FileNonEmpty(path);

    if (!img_.Open(path, device_pages_ * kPageStride)) {
        LOG(Caution, "Imx51NandStore: cannot open '%s'\n", path.c_str());
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }

    emu_.Get<HostWidgetRegistry>().Register(this);

    if (existed) {
        LOG(SocNand, "Imx51NandStore: using existing '%s' (%llu pages)\n",
            path.c_str(), static_cast<unsigned long long>(device_pages_));
        return;
    }

    auto* sf = emu_.TryGet<SecFlash>();
    if (!sf || !sf->IsPresent()) {
        LOG(Caution, "Imx51NandStore: no '%s' and no `.sec` to seed/flash from\n",
            path.c_str());
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
    LOG(SocNand, "Imx51NandStore: synthesizing '%s' - seeding factory boot region\n",
        path.c_str());
    Seed();
}

void Imx51NandStore::ReadPage(uint64_t main_off, uint8_t* main, uint8_t* spare) {
    MarkRx();
    const uint64_t page = main_off / kMainBytes;
    std::array<uint8_t, kPageStride> buf{};
    if (page >= device_pages_ ||
        !img_.ReadSectors(page * kSectorsPerPage, kSectorsPerPage, buf.data())) {
        std::memset(main,  0xFF, kMainBytes);
        std::memset(spare, 0xFF, kSpareBytes);
        return;
    }
    for (auto& b : buf) b = static_cast<uint8_t>(~b);
    std::memcpy(main,  buf.data(),              kMainBytes);
    std::memcpy(spare, buf.data() + kMainBytes, kSpareBytes);
}

void Imx51NandStore::WritePage(uint64_t main_off, const uint8_t* main, const uint8_t* spare) {
    MarkTx();
    const uint64_t page = main_off / kMainBytes;
    if (page >= device_pages_) {
        LOG(Caution, "Imx51NandStore: program out-of-range page %llu (off 0x%llX)\n",
            static_cast<unsigned long long>(page), static_cast<unsigned long long>(main_off));
        return;
    }
    std::array<uint8_t, kPageStride> buf{};
    std::memcpy(buf.data(),              main,  kMainBytes);
    std::memcpy(buf.data() + kMainBytes, spare, kSpareBytes);
    for (auto& b : buf) b = static_cast<uint8_t>(~b);
    if (!img_.WriteSectors(page * kSectorsPerPage, kSectorsPerPage, buf.data()))
        LOG(Caution, "Imx51NandStore: program write-fail page %llu\n",
            static_cast<unsigned long long>(page));
}

void Imx51NandStore::EraseBlock(uint64_t main_off) {
    const uint64_t first_page = (main_off / kBlock) * kPagesPerBlock;
    img_.PunchHole(first_page * kSectorsPerPage, kPagesPerBlock * kSectorsPerPage);
}

void Imx51NandStore::ReadMain(uint64_t main_off, void* dst, uint32_t len) {
    const uint64_t page  = main_off / kMainBytes;
    const uint32_t in_pg = static_cast<uint32_t>(main_off % kMainBytes);
    std::array<uint8_t, kMainBytes>  mbuf{};
    std::array<uint8_t, kSpareBytes> sbuf{};
    ReadPage(page * kMainBytes, mbuf.data(), sbuf.data());
    const uint32_t n = (in_pg + len <= kMainBytes) ? len : (kMainBytes - in_pg);
    std::memcpy(dst, mbuf.data() + in_pg, n);
    if (n < len) std::memset(static_cast<uint8_t*>(dst) + n, 0xFF, len - n);
}

void Imx51NandStore::Seed() {
    auto& layout = emu_.Get<Imx51NandLayout>();
    const uint64_t os_start   = layout.OsRegionStartBlock();
    const uint64_t dev_blocks = layout.DeviceBlocks();
    uint64_t seeded = 0;
    auto seed_block = [&](uint64_t blk) {
        for (uint32_t p = 0; p < kPagesPerBlock; ++p) {
            const uint64_t main_off = blk * kBlock + static_cast<uint64_t>(p) * kMainBytes;
            std::array<uint8_t, kMainBytes>  main{};
            std::array<uint8_t, kSpareBytes> spare{};
            if (layout.BuildFactoryPage(main_off, main.data(), kMainBytes,
                                        spare.data(), kSpareBytes)) {
                WritePage(main_off, main.data(), spare.data());
                ++seeded;
            }
        }
    };
    for (uint64_t blk = 0; blk < os_start; ++blk) seed_block(blk);
    if (dev_blocks >= 5) seed_block(dev_blocks - 5);   /* synthesized BBT */
    if (dev_blocks >= 1) seed_block(dev_blocks - 1);   /* synthesized DPS */
    LOG(SocNand, "Imx51NandStore: seeded %llu pages (boot region blocks 0..%llu + BBT/DPS)\n",
        static_cast<unsigned long long>(seeded),
        static_cast<unsigned long long>(os_start ? os_start - 1 : 0));
}

void Imx51NandStore::DrawIcon(HDC dc, const RECT& box) const {
    DrawChipIcon(dc, box);
}

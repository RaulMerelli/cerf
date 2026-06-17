#pragma once
#include "service.h"
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

struct DeviceMeta {
    std::string device_name;
    std::string board_name;
    std::string soc_family;
    std::string os_name;
    int         os_ver_major = 0;
    int         os_ver_minor = 0;
    int         device_year  = 0;
};

/* One entry of cerf.json "additional_packages.compact_flash_cards": a CF
   image file shipped inside the device directory plus its insert-menu
   display name. */
struct BundledCompactFlashCard {
    std::string file;   /* image filename, relative to the device directory */
    std::string name;   /* display name; menu shows "Insert bundled CF: <name>" */
};

/* Boot action when a saved state image exists in the device directory
   (--boot=resume|cold|warm). */
enum class StateBootMode {
    Resume,   /* full restore from state.img if present, else cold boot */
    Warm,     /* RAM + flash only (OS reboots) if present, else cold boot */
    Cold,     /* ignore state.img, cold boot */
};

struct DeviceConfig : public Service {
    using Service::Service;

    /* Populates every field below from the global + per-device cerf.json and
       the CLI, via ConfigLoader. Lazy: runs on first emu_.Get<DeviceConfig>(). */
    void OnReady() override;

    std::string device_name;

    DeviceMeta meta;

    uint32_t board_configurable_screen_width  = 800;
    uint32_t board_configurable_screen_height = 600;

    /* True when the configurable screen size came from cerf.json
       board.configurable_screen_* or --screen-width/height (not the bare
       default above). The host window uses this to let an explicit size win
       over a board's fixed-LCD preferred window size. */
    bool board_configurable_screen_explicit = false;

    bool        network_enabled = true;
    std::string network_mac     = "02:CE:5F:00:00:01";
    uint32_t    network_mtu     = 1500;
    std::string network_forward_tcp;
    std::string network_forward_udp;

    std::string              rom_primary;
    std::vector<std::string> rom_extensions;
    std::string              rom_recovery;

    /* Optional serial-config EEPROM image (cerf.json rom.eeprom). A board's
       SSP/SPI EEPROM peripheral loads it from the device directory; empty
       when the device has none. */
    std::string              rom_eeprom;

    /* Optional CF images bundled with the ROM (cerf.json
       "additional_packages.compact_flash_cards"); the CF insert menu offers
       each entry whose file is present in the device directory. */
    std::vector<BundledCompactFlashCard> bundled_compact_flash_cards;

    bool boot_in_recovery = false;
    bool guest_additions = false;

    /* HwScreen boot animation (CERF/OEM logo intro). Dev builds default OFF so
       debug output shows instantly; production defaults ON. --boot-anim=
       enable|disable overrides either way. */
    bool boot_anim =
#if CERF_DEV_MODE
        false;
#else
        true;
#endif

    /* Dev builds default to cold so iteration never auto-resumes a stale
       image; production resumes a saved state. --boot overrides either. */
    StateBootMode boot_mode =
#if CERF_DEV_MODE
        StateBootMode::Cold;
#else
        StateBootMode::Resume;
#endif

    /* --share-folder=<host path>: pre-enables the guest-additions folder share
       on a host directory at boot (the widget still toggles it live). Empty when
       not given on the command line. */
    std::string share_folder;

    /* --full-screen: enter borderless fullscreen (the Right Ctrl+F toggle) right
       after the host window is shown. CLI-only launch preference, default off. */
    bool start_fullscreen = false;

    /* Guest-additions ROM-module substitutions from the GLOBAL cerf.json
       ("global_substitutions_inside_rom"): {ROM module name -> ce_apps DLL}.
       GuestAdditionsInjector replaces each present ROM module with the named
       CERF-built binary from build/<cfg>/Win32/ce_apps/. */
    std::vector<std::pair<std::string, std::string>> global_rom_substitutions;

    /* When true, ConfigLoader overwrites board_configurable_screen_* with the
       host monitor size (host_w-10 x host_h-40, capped 3840x2160) so a
       guest-additions display fills the host screen. Read from cerf.json. */
    bool adopt_guest_additions_resolution_for_host_screen = false;

    /* Default state of the shutdown dialog's "Save the state" checkbox. Read
       from the GLOBAL cerf.json top-level "last_save_state_mode"; the dialog's
       "Remember choice" checkbox writes the user's selection back to that file
       via ConfigLoader. Defaults off so a close never auto-saves silently. */
    bool last_save_state_mode = false;
};

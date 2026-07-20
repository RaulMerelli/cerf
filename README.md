# **CE Runtime Foundation** v6.7 pre-alpha

<p align="center">
  <a href="https://cerf.cx">
    <img src="gweslab.png" width="400" alt="cerf.cx" />
  </a>
</p>

<p align="center">
  <b><a href="https://cerf.cx">cerf.cx</a></b> - read more about the project
</p>

<br/>

A universal Windows CE emulator: a virtual hardware platform that boots real CE and Windows Mobile ROMs on modern Windows.

[![Discord](https://img.shields.io/badge/Discord-join%20the%20server-5865F2?logo=discord&logoColor=white)](https://discord.gg/QREE9Y2v2d) [![Patreon](https://img.shields.io/badge/Patreon-support-FF424D?logo=patreon&logoColor=white)](https://www.patreon.com/dz3n)

## Downloads

Download the WIP build (6.7) from artifacts [![build](https://github.com/gweslab/cerf/actions/workflows/build.yml/badge.svg)](https://github.com/gweslab/cerf/actions/workflows/build.yml) to use all the latest features, or go to the [latest release](https://github.com/gweslab/cerf/releases/latest).

Run **`launcher.exe`**: pick a device, and it downloads the ROM bundle and boots it. Running `cerf.exe --device=...` directly, its command line and its logs are covered [in the articles](https://cerf.cx/articles/command-line/).

## Supported boards

<table>
  <thead>
    <tr>
      <th>SoC</th>
      <th>Board / OS</th>
      <th>Features</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td align="center"><img src="launcher/assets/icons/badge_mips.png" align="middle" title="MIPS" alt="MIPS"/><br/><b>NEC VR4121</b><br/><sub>MIPS III</sub></td>
      <td>
        <img src="cerf/assets/icons_sources/board.svg" width="16" height="16" title="PDA" alt="PDA"/> <b>Casio Toricomail</b> <code>casio_toricomail</code><br/>
        Windows CE 2.12
      </td>
      <td><img src="cerf/assets/icons_sources/display.svg" width="32" height="32" title="Display" alt="Display"/> <img src="cerf/assets/icons_sources/stylus.svg" width="32" height="32" title="Touch" alt="Touch"/> <img src="cerf/assets/icons_sources/keyboard.svg" width="32" height="32" title="Keyboard" alt="Keyboard"/></td>
    </tr>
    <tr>
      <td rowspan="2" align="center"><img src="launcher/assets/icons/badge_arm.png" align="middle" title="ARM" alt="ARM"/><br/><b>Intel XScale PXA255</b><br/><sub>ARMv5TE</sub></td>
      <td>
        <img src="cerf/assets/icons_sources/board.svg" width="16" height="16" title="PDA" alt="PDA"/> <b>Falcon 4220</b> <code>falcon_4220</code><br/>
        Windows CE .NET
      </td>
      <td><img src="cerf/assets/icons_sources/display.svg" width="32" height="32" title="Display" alt="Display"/> <img src="cerf/assets/icons_sources/stylus.svg" width="32" height="32" title="Touch" alt="Touch"/> <img src="cerf/assets/icons_sources/suspend.svg" width="32" height="32" title="Suspend / Resume" alt="Suspend / Resume"/> <img src="cerf/assets/icons_sources/ga_autoresize.svg" width="32" height="32" title="Guest Additions" alt="Guest Additions"/> <img src="cerf/assets/icons_sources/speaker_active.svg" width="32" height="32" title="Sound" alt="Sound"/> <img src="cerf/assets/icons_sources/pcmcia_enabled.svg" width="32" height="32" title="PCMCIA" alt="PCMCIA"/> <img src="cerf/assets/icons_sources/internet.svg" width="32" height="32" title="Network" alt="Network"/> <img src="cerf/assets/icons_sources/battery.svg" width="32" height="32" title="Battery" alt="Battery"/></td>
    </tr>
    <tr>
      <td>
        <img src="cerf/assets/icons_sources/board.svg" width="16" height="16" title="PDA" alt="PDA"/> <b>NEC MobilePro 900</b> <code>nec_mobilepro_900</code><br/>
        Handheld PC 2000<br/>
        Windows CE .NET
      </td>
      <td><img src="cerf/assets/icons_sources/display.svg" width="32" height="32" title="Display" alt="Display"/> <img src="cerf/assets/icons_sources/stylus.svg" width="32" height="32" title="Touch" alt="Touch"/> <img src="cerf/assets/icons_sources/keyboard.svg" width="32" height="32" title="Keyboard" alt="Keyboard"/> <img src="cerf/assets/icons_sources/ga_autoresize.svg" width="32" height="32" title="Guest Additions" alt="Guest Additions"/> <img src="cerf/assets/icons_sources/speaker_active.svg" width="32" height="32" title="Sound" alt="Sound"/> <img src="cerf/assets/icons_sources/pcmcia_enabled.svg" width="32" height="32" title="PCMCIA" alt="PCMCIA"/> <img src="cerf/assets/icons_sources/internet.svg" width="32" height="32" title="Network" alt="Network"/></td>
    </tr>
    <tr>
      <td align="center"><img src="launcher/assets/icons/badge_arm.png" align="middle" title="ARM" alt="ARM"/><br/><b>Freescale i.MX51</b><br/><sub>Cortex-A8</sub></td>
      <td>
        <img src="cerf/assets/icons_sources/board.svg" width="16" height="16" title="PDA" alt="PDA"/> <b>Ford SYNC 2</b> <code>ford_sync_2</code><br/>
        Windows CE 6
      </td>
      <td><img src="cerf/assets/icons_sources/display.svg" width="32" height="32" title="Display" alt="Display"/> <img src="cerf/assets/icons_sources/stylus.svg" width="32" height="32" title="Touch" alt="Touch"/></td>
    </tr>
    <tr>
      <td rowspan="4" align="center"><img src="launcher/assets/icons/badge_arm.png" align="middle" title="ARM" alt="ARM"/><br/><b>Intel SA-1110</b><br/><sub>StrongARM</sub></td>
      <td>
        <img src="cerf/assets/icons_sources/board.svg" width="16" height="16" title="PDA" alt="PDA"/> <b>HP Jornada 720</b> <code>jornada_720</code><br/>
        Handheld PC 2000
      </td>
      <td><img src="cerf/assets/icons_sources/display.svg" width="32" height="32" title="Display" alt="Display"/> <img src="cerf/assets/icons_sources/stylus.svg" width="32" height="32" title="Touch" alt="Touch"/> <img src="cerf/assets/icons_sources/keyboard.svg" width="32" height="32" title="Keyboard" alt="Keyboard"/> <img src="cerf/assets/icons_sources/suspend.svg" width="32" height="32" title="Suspend / Resume" alt="Suspend / Resume"/> <img src="cerf/assets/icons_sources/ga_autoresize.svg" width="32" height="32" title="Guest Additions" alt="Guest Additions"/> <img src="cerf/assets/icons_sources/speaker_active.svg" width="32" height="32" title="Sound" alt="Sound"/> <img src="cerf/assets/icons_sources/pcmcia_enabled.svg" width="32" height="32" title="PCMCIA" alt="PCMCIA"/> <img src="cerf/assets/icons_sources/internet.svg" width="32" height="32" title="Network" alt="Network"/> <img src="cerf/assets/icons_sources/battery.svg" width="32" height="32" title="Battery" alt="Battery"/></td>
    </tr>
    <tr>
      <td>
        <img src="cerf/assets/icons_sources/board.svg" width="16" height="16" title="PDA" alt="PDA"/> <b>iPAQ H3100/H3600/H3700</b> <code>ipaq_gen1</code><br/>
        Pocket PC 2000<br/>
        Pocket PC 2002
      </td>
      <td><img src="cerf/assets/icons_sources/display.svg" width="32" height="32" title="Display" alt="Display"/> <img src="cerf/assets/icons_sources/stylus.svg" width="32" height="32" title="Touch" alt="Touch"/> <img src="cerf/assets/icons_sources/suspend.svg" width="32" height="32" title="Suspend / Resume" alt="Suspend / Resume"/> <img src="cerf/assets/icons_sources/ga_autoresize.svg" width="32" height="32" title="Guest Additions" alt="Guest Additions"/> <img src="cerf/assets/icons_sources/speaker_active.svg" width="32" height="32" title="Sound" alt="Sound"/> <img src="cerf/assets/icons_sources/microphone.svg" width="32" height="32" title="Microphone" alt="Microphone"/> <img src="cerf/assets/icons_sources/pcmcia_enabled.svg" width="32" height="32" title="PCMCIA" alt="PCMCIA"/> <img src="cerf/assets/icons_sources/internet.svg" width="32" height="32" title="Network" alt="Network"/></td>
    </tr>
    <tr>
      <td>
        <img src="cerf/assets/icons_sources/board.svg" width="16" height="16" title="PDA" alt="PDA"/> <b>Siemens SIMpad SL4</b> <code>simpad_sl4</code><br/>
        Handheld PC 2000<br/>
        Windows CE .NET
      </td>
      <td><img src="cerf/assets/icons_sources/display.svg" width="32" height="32" title="Display" alt="Display"/> <img src="cerf/assets/icons_sources/stylus.svg" width="32" height="32" title="Touch" alt="Touch"/> <img src="cerf/assets/icons_sources/ga_autoresize.svg" width="32" height="32" title="Guest Additions" alt="Guest Additions"/> <img src="cerf/assets/icons_sources/speaker_active.svg" width="32" height="32" title="Sound" alt="Sound"/> <img src="cerf/assets/icons_sources/pcmcia_enabled.svg" width="32" height="32" title="PCMCIA" alt="PCMCIA"/> <img src="cerf/assets/icons_sources/internet.svg" width="32" height="32" title="Network" alt="Network"/> <img src="cerf/assets/icons_sources/battery.svg" width="32" height="32" title="Battery" alt="Battery"/></td>
    </tr>
    <tr>
      <td>
        <img src="cerf/assets/icons_sources/board.svg" width="16" height="16" title="PDA" alt="PDA"/> <b>SmartBook G138</b> <code>smartbook_g138</code><br/>
        Windows CE .NET
      </td>
      <td><img src="cerf/assets/icons_sources/display.svg" width="32" height="32" title="Display" alt="Display"/> <img src="cerf/assets/icons_sources/stylus.svg" width="32" height="32" title="Touch" alt="Touch"/> <img src="cerf/assets/icons_sources/keyboard.svg" width="32" height="32" title="Keyboard" alt="Keyboard"/> <img src="cerf/assets/icons_sources/suspend.svg" width="32" height="32" title="Suspend / Resume" alt="Suspend / Resume"/> <img src="cerf/assets/icons_sources/ga_autoresize.svg" width="32" height="32" title="Guest Additions" alt="Guest Additions"/> <img src="cerf/assets/icons_sources/pcmcia_enabled.svg" width="32" height="32" title="PCMCIA" alt="PCMCIA"/> <img src="cerf/assets/icons_sources/internet.svg" width="32" height="32" title="Network" alt="Network"/></td>
    </tr>
    <tr>
      <td align="center"><img src="launcher/assets/icons/badge_arm.png" align="middle" title="ARM" alt="ARM"/><br/><b>Intel SA-1100</b><br/><sub>StrongARM</sub></td>
      <td>
        <img src="cerf/assets/icons_sources/board.svg" width="16" height="16" title="PDA" alt="PDA"/> <b>HP Jornada 820</b> <code>jornada_820</code><br/>
        Handheld PC 3.0 Professional
      </td>
      <td><img src="cerf/assets/icons_sources/display.svg" width="32" height="32" title="Display" alt="Display"/> <img src="cerf/assets/icons_sources/cursor.svg" width="32" height="32" title="Mouse" alt="Mouse"/> <img src="cerf/assets/icons_sources/keyboard.svg" width="32" height="32" title="Keyboard" alt="Keyboard"/> <img src="cerf/assets/icons_sources/suspend.svg" width="32" height="32" title="Suspend / Resume" alt="Suspend / Resume"/> <img src="cerf/assets/icons_sources/ga_autoresize.svg" width="32" height="32" title="Guest Additions" alt="Guest Additions"/> <img src="cerf/assets/icons_sources/speaker_active.svg" width="32" height="32" title="Sound" alt="Sound"/> <img src="cerf/assets/icons_sources/pcmcia_enabled.svg" width="32" height="32" title="PCMCIA" alt="PCMCIA"/> <img src="cerf/assets/icons_sources/internet.svg" width="32" height="32" title="Network" alt="Network"/> <img src="cerf/assets/icons_sources/battery.svg" width="32" height="32" title="Battery" alt="Battery"/></td>
    </tr>
    <tr>
      <td align="center"><img src="launcher/assets/icons/badge_arm.png" align="middle" title="ARM" alt="ARM"/><br/><b>ARM720T</b><br/><sub>ARMv4T</sub></td>
      <td>
        <img src="cerf/assets/icons_sources/board.svg" width="16" height="16" title="PDA" alt="PDA"/> <b>Microsoft Windows CE Hardware Reference Platform</b> <code>odo</code><br/>
        Windows CE 2.11<br/>
        Windows CE 3
      </td>
      <td><img src="cerf/assets/icons_sources/display.svg" width="32" height="32" title="Display" alt="Display"/> <img src="cerf/assets/icons_sources/stylus.svg" width="32" height="32" title="Touch" alt="Touch"/> <img src="cerf/assets/icons_sources/keyboard.svg" width="32" height="32" title="Keyboard" alt="Keyboard"/> <img src="cerf/assets/icons_sources/ga_autoresize.svg" width="32" height="32" title="Guest Additions" alt="Guest Additions"/> <img src="cerf/assets/icons_sources/speaker_active.svg" width="32" height="32" title="Sound" alt="Sound"/></td>
    </tr>
    <tr>
      <td align="center"><img src="launcher/assets/icons/badge_mips.png" align="middle" title="MIPS" alt="MIPS"/><br/><b>NEC VR4102</b><br/><sub>MIPS III</sub></td>
      <td>
        <img src="cerf/assets/icons_sources/board.svg" width="16" height="16" title="PDA" alt="PDA"/> <b>NEC MobilePro 700</b> <code>nec_mobilepro_700</code><br/>
        Windows CE 2.0
      </td>
      <td><img src="cerf/assets/icons_sources/display.svg" width="32" height="32" title="Display" alt="Display"/> <img src="cerf/assets/icons_sources/stylus.svg" width="32" height="32" title="Touch" alt="Touch"/> <img src="cerf/assets/icons_sources/keyboard.svg" width="32" height="32" title="Keyboard" alt="Keyboard"/> <img src="cerf/assets/icons_sources/suspend.svg" width="32" height="32" title="Suspend / Resume" alt="Suspend / Resume"/> <img src="cerf/assets/icons_sources/ga_autoresize.svg" width="32" height="32" title="Guest Additions" alt="Guest Additions"/> <img src="cerf/assets/icons_sources/pcmcia_enabled.svg" width="32" height="32" title="PCMCIA" alt="PCMCIA"/> <img src="cerf/assets/icons_sources/internet.svg" width="32" height="32" title="Network" alt="Network"/> <img src="cerf/assets/icons_sources/serial_com.svg" width="32" height="32" title="Serial Port" alt="Serial Port"/></td>
    </tr>
    <tr>
      <td align="center"><img src="launcher/assets/icons/badge_mips.png" align="middle" title="MIPS" alt="MIPS"/><br/><b>NEC VR5500</b><br/><sub>MIPS IV</sub></td>
      <td>
        <img src="cerf/assets/icons_sources/board.svg" width="16" height="16" title="PDA" alt="PDA"/> <b>NEC Rockhopper SG2_VR5500</b> <code>nec_rockhopper</code><br/>
        Windows CE 6
      </td>
      <td><img src="cerf/assets/icons_sources/display.svg" width="32" height="32" title="Display" alt="Display"/> <img src="cerf/assets/icons_sources/cursor.svg" width="32" height="32" title="Mouse" alt="Mouse"/> <img src="cerf/assets/icons_sources/keyboard.svg" width="32" height="32" title="Keyboard" alt="Keyboard"/> <img src="cerf/assets/icons_sources/ga_autoresize.svg" width="32" height="32" title="Guest Additions" alt="Guest Additions"/> <img src="cerf/assets/icons_sources/pcmcia_enabled.svg" width="32" height="32" title="PCMCIA" alt="PCMCIA"/> <img src="cerf/assets/icons_sources/internet.svg" width="32" height="32" title="Network" alt="Network"/></td>
    </tr>
    <tr>
      <td align="center"><img src="launcher/assets/icons/badge_arm.png" align="middle" title="ARM" alt="ARM"/><br/><b>TI OMAP 3530</b><br/><sub>Cortex-A8</sub></td>
      <td>
        <img src="cerf/assets/icons_sources/board.svg" width="16" height="16" title="PDA" alt="PDA"/> <b>OMAP 3530 EVM</b> <code>omap_3530_evm</code><br/>
        Windows CE 7
      </td>
      <td><img src="cerf/assets/icons_sources/display.svg" width="32" height="32" title="Display" alt="Display"/> <img src="cerf/assets/icons_sources/stylus.svg" width="32" height="32" title="Touch" alt="Touch"/> <img src="cerf/assets/icons_sources/ga_autoresize.svg" width="32" height="32" title="Guest Additions" alt="Guest Additions"/> <img src="cerf/assets/icons_sources/speaker_active.svg" width="32" height="32" title="Sound" alt="Sound"/></td>
    </tr>
    <tr>
      <td rowspan="2" align="center"><img src="launcher/assets/icons/badge_mips.png" align="middle" title="MIPS" alt="MIPS"/><br/><b>Philips PR31700</b><br/><sub>MIPS I</sub></td>
      <td>
        <img src="cerf/assets/icons_sources/board.svg" width="16" height="16" title="PDA" alt="PDA"/> <b>Philips Nino 300</b> <code>philips_nino_300</code><br/>
        Palm-size PC
      </td>
      <td><img src="cerf/assets/icons_sources/display.svg" width="32" height="32" title="Display" alt="Display"/> <img src="cerf/assets/icons_sources/stylus.svg" width="32" height="32" title="Touch" alt="Touch"/> <img src="cerf/assets/icons_sources/keyboard.svg" width="32" height="32" title="Keyboard" alt="Keyboard"/> <img src="cerf/assets/icons_sources/suspend.svg" width="32" height="32" title="Suspend / Resume" alt="Suspend / Resume"/> <img src="cerf/assets/icons_sources/ga_autoresize.svg" width="32" height="32" title="Guest Additions" alt="Guest Additions"/> <img src="cerf/assets/icons_sources/speaker_active.svg" width="32" height="32" title="Sound" alt="Sound"/> <img src="cerf/assets/icons_sources/pcmcia_enabled.svg" width="32" height="32" title="PCMCIA" alt="PCMCIA"/> <img src="cerf/assets/icons_sources/battery.svg" width="32" height="32" title="Battery" alt="Battery"/> <img src="cerf/assets/icons_sources/serial_com.svg" width="32" height="32" title="Serial Port" alt="Serial Port"/></td>
    </tr>
    <tr>
      <td>
        <img src="cerf/assets/icons_sources/board.svg" width="16" height="16" title="PDA" alt="PDA"/> <b>Sharp Mobilon HC-4100</b> <code>sharp_mobilon_hc4100</code><br/>
        Windows CE 2.0
      </td>
      <td><img src="cerf/assets/icons_sources/display.svg" width="32" height="32" title="Display" alt="Display"/> <img src="cerf/assets/icons_sources/stylus.svg" width="32" height="32" title="Touch" alt="Touch"/> <img src="cerf/assets/icons_sources/keyboard.svg" width="32" height="32" title="Keyboard" alt="Keyboard"/> <img src="cerf/assets/icons_sources/ga_autoresize.svg" width="32" height="32" title="Guest Additions" alt="Guest Additions"/> <img src="cerf/assets/icons_sources/speaker_active.svg" width="32" height="32" title="Sound" alt="Sound"/> <img src="cerf/assets/icons_sources/pcmcia_enabled.svg" width="32" height="32" title="PCMCIA" alt="PCMCIA"/> <img src="cerf/assets/icons_sources/internet.svg" width="32" height="32" title="Network" alt="Network"/> <img src="cerf/assets/icons_sources/battery.svg" width="32" height="32" title="Battery" alt="Battery"/> <img src="cerf/assets/icons_sources/serial_com.svg" width="32" height="32" title="Serial Port" alt="Serial Port"/></td>
    </tr>
    <tr>
      <td align="center"><img src="launcher/assets/icons/badge_mips.png" align="middle" title="MIPS" alt="MIPS"/><br/><b>Philips PR31500</b><br/><sub>MIPS I</sub></td>
      <td>
        <img src="cerf/assets/icons_sources/board.svg" width="16" height="16" title="PDA" alt="PDA"/> <b>Philips Velo 1</b> <code>philips_velo_1</code><br/>
        Windows CE 1.0
      </td>
      <td><img src="cerf/assets/icons_sources/display.svg" width="32" height="32" title="Display" alt="Display"/> <img src="cerf/assets/icons_sources/stylus.svg" width="32" height="32" title="Touch" alt="Touch"/> <img src="cerf/assets/icons_sources/keyboard.svg" width="32" height="32" title="Keyboard" alt="Keyboard"/> <img src="cerf/assets/icons_sources/suspend.svg" width="32" height="32" title="Suspend / Resume" alt="Suspend / Resume"/> <img src="cerf/assets/icons_sources/speaker_active.svg" width="32" height="32" title="Sound" alt="Sound"/> <img src="cerf/assets/icons_sources/pcmcia_enabled.svg" width="32" height="32" title="PCMCIA" alt="PCMCIA"/> <img src="cerf/assets/icons_sources/internet.svg" width="32" height="32" title="Network" alt="Network"/> <img src="cerf/assets/icons_sources/battery.svg" width="32" height="32" title="Battery" alt="Battery"/> <img src="cerf/assets/icons_sources/serial_com.svg" width="32" height="32" title="Serial Port" alt="Serial Port"/></td>
    </tr>
    <tr>
      <td rowspan="2" align="center"><img src="launcher/assets/icons/badge_arm.png" align="middle" title="ARM" alt="ARM"/><br/><b>Samsung S3C2410</b><br/><sub>ARM920T</sub></td>
      <td>
        <img src="cerf/assets/icons_sources/board.svg" width="16" height="16" title="PDA" alt="PDA"/> <b>Siemens P177</b> <code>siemens_p177</code><br/>
        Windows CE 5
      </td>
      <td><img src="cerf/assets/icons_sources/display.svg" width="32" height="32" title="Display" alt="Display"/> <img src="cerf/assets/icons_sources/stylus.svg" width="32" height="32" title="Touch" alt="Touch"/> <img src="cerf/assets/icons_sources/ga_autoresize.svg" width="32" height="32" title="Guest Additions" alt="Guest Additions"/></td>
    </tr>
    <tr>
      <td>
        <img src="cerf/assets/icons_sources/board.svg" width="16" height="16" title="PDA" alt="PDA"/> <b>Device Emulator</b> <code>devemu</code><br/>
        Windows CE 6<br/>
        Windows Mobile 5<br/>
        Windows Mobile 6<br/>
        WM 2003 SE<br/>
        Windows CE 5
      </td>
      <td><img src="cerf/assets/icons_sources/display.svg" width="32" height="32" title="Display" alt="Display"/> <img src="cerf/assets/icons_sources/stylus.svg" width="32" height="32" title="Touch" alt="Touch"/> <img src="cerf/assets/icons_sources/keyboard.svg" width="32" height="32" title="Keyboard" alt="Keyboard"/> <img src="cerf/assets/icons_sources/suspend.svg" width="32" height="32" title="Suspend / Resume" alt="Suspend / Resume"/> <img src="cerf/assets/icons_sources/ga_autoresize.svg" width="32" height="32" title="Guest Additions" alt="Guest Additions"/> <img src="cerf/assets/icons_sources/speaker_active.svg" width="32" height="32" title="Sound" alt="Sound"/> <img src="cerf/assets/icons_sources/pcmcia_enabled.svg" width="32" height="32" title="PCMCIA" alt="PCMCIA"/> <img src="cerf/assets/icons_sources/internet.svg" width="32" height="32" title="Network" alt="Network"/> <img src="cerf/assets/icons_sources/battery.svg" width="32" height="32" title="Battery" alt="Battery"/></td>
    </tr>
    <tr>
      <td align="center"><img src="launcher/assets/icons/badge_arm.png" align="middle" title="ARM" alt="ARM"/><br/><b>Freescale i.MX31L</b><br/><sub>ARM1136</sub></td>
      <td>
        <img src="cerf/assets/icons_sources/board.svg" width="16" height="16" title="PDA" alt="PDA"/> <b>Zune 30</b> <code>zune_30</code><br/>
        Windows CE 5
      </td>
      <td><img src="cerf/assets/icons_sources/display.svg" width="32" height="32" title="Display" alt="Display"/> <img src="cerf/assets/icons_sources/keyboard.svg" width="32" height="32" title="Keyboard" alt="Keyboard"/> <img src="cerf/assets/icons_sources/ga_autoresize.svg" width="32" height="32" title="Guest Additions" alt="Guest Additions"/> <img src="cerf/assets/icons_sources/speaker_active.svg" width="32" height="32" title="Sound" alt="Sound"/></td>
    </tr>
  </tbody>
</table>

## Running your own ROM

A ROM boots only if **that exact board is implemented in CERF** - a matching SoC is not enough. Dropping in your own dump of an already-supported board is covered [in the articles](https://cerf.cx/articles/own-rom/).

Bringing up a board CERF does **not** support is emulator development: the board's memory map, every peripheral its drivers touch, the SoC quirks - all of it implemented in C++, grounded in datasheets, BSP sources and RE, at the quality bar CERF already ships. It is not a config tweak, and not something to hand to an AI and expect magic.

> [!IMPORTANT]
> **CERF does not accept ROM submissions / board implementation requests.**
> The devices worth doing are done, and so are several that cost months of work and that essentially nobody needs. Bringing up one more board to arrive at one more Windows CE desktop is a very large amount of work for an outcome that already exists. **Further submissions will be declined**, unless they are genuinely interesting or in demand and I want to do the work.

## Building

Requires Visual Studio 2026 with the C++ desktop development workload.

> [!NOTE]
> **First build on a fresh machine takes 1+ hour.** vcpkg compiles dependencies from source before CERF starts linking. This happens once per machine - subsequent builds reuse the cached `vcpkg_installed/` tree and finish in a few minutes. Do not interrupt the first build.

Set up the clone (once per machine):

```
setup.cmd
```

This initialises submodules, points git at the repo's tracked hooks
(`core.hooksPath` = `.githooks` - git does not clone hook config, so hooks are
inert in a fresh clone until this runs), and reports any missing prerequisite
(Python launcher, vcpkg MSBuild integration). Re-run it any time; it is
idempotent. `setup.cmd -Check` reports status without changing anything.

Build via the helper script:

```
powershell -ExecutionPolicy Bypass -File build.ps1
```

Or invoke msbuild directly:

```
msbuild cerf.sln /p:Configuration=Release /p:Platform=Win32
```

### Building the CE-side binaries (optional)

`ce_apps/` holds the Windows CE binaries CERF ships, including the Guest Additions display
driver. Building them needs a CE toolchain and SDK, which are **not** required for
`cerf.exe` itself - if you are working on the emulator core, boards, SoCs, the JIT or the
host UI, ignore this and use the prebuilt binaries.

To build them, install eMbedded Visual C++ 4.0 (a free, officially archived Microsoft
download) and run one script. Full instructions: **[docs/ce_apps_setup.md](docs/ce_apps_setup.md)**.

`setup.cmd -Check` reports whether the CE toolchain is present.

The website is built from `docs/website/` - `python tools/build_site.py --serve` runs it locally with live reload.

## Changelog

<table>
  <thead>
    <tr>
      <th>Version</th>
      <th>Release Date</th>
      <th>Changes</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td>v6.7</td>
      <td>TBA</td>
      <td>
        <p><b>📱 Devices</b><br/>
          ✅ Sharp Mobilon HC-4100: fixed suspend crash</p>
        <p><b>💿 Emulator</b><br/>
          🆕 Discord Rich Presence - shows the current device and OS in your Discord profile<br/>
          ✅ Fixed framebuffer not relatching on suspend/resume<br/>
          ✅ Fixed 100% CPU usage and UI deadlocks on Windows XP on non-framebuffer tabs</p>
        <p><b>🚀 Launcher</b><br/>
          🆕 UI refresh<br/>
          ✅ Remote metadata-only updates no longer mark installed bundles as pending an update</p>
        <p><b>💾 CE Apps</b><br/>
          ✅ CerfDemo: UI and performance improvements</p>
        <p><b>✨ Guest Additions</b><br/>
          🆕 High refresh rate support: the guest video mode and host window scanout follow the host monitor&#x27;s refresh rate<br/>
          🆕 --screen-refresh-rate flag to set the refresh rate manually<br/>
          ✅ Input devices now run at the proper priority, staying responsive under heavy guest CPU load</p>
      </td>
    </tr>
    <tr>
      <td>v6.6</td>
      <td>18 Jul 2026</td>
      <td>
        <p><b>📱 Devices</b><br/>
          ✅ Fixed Device Emulator crash booting Windows Mobile 5.2 ROMs</p>
        <p><b>💿 Emulator</b><br/>
          ✅ Device names with spaces and absolute rom.* paths in cerf.json are now supported</p>
        <p><b>🚀 Launcher</b><br/>
          🆕 <b>New-device wizard</b>: create a device profile from your local ROM dump<br/>
          🆕 Rename a device from its right-click menu</p>
      </td>
    </tr>
    <tr>
      <td>v6.5</td>
      <td>17 Jul 2026</td>
      <td>
        <p><b>📱 Devices</b><br/>
          🆕 Sharp Mobilon HC-4100 support (Handheld PC, Windows CE 2.0)<br/>
          🆕 Casio Toricomail support</p>
        <p><b>💿 Emulator</b><br/>
          ✅ Fixed Integer scale 2x/3x not resizing the window under &quot;Match guest size&quot;<br/>
          ✅ Fixed moving the window releasing the &quot;Match guest size&quot; lock (now only a resize does)</p>
        <p><b>🚀 Launcher</b><br/>
          🆕 Added downloads count sort</p>
        <p><b>💾 CE Apps</b><br/>
          ✅ Fixed bundled CE2 apps (ROM dumper and others) crashing on launch - their coredll imports were bound by version-specific ordinal instead of by name</p>
        <p><b>✨ Guest Additions</b><br/>
          ✅ Display driver unified onto a single mips1 build across MIPS devices</p>
      </td>
    </tr>
    <tr>
      <td>v6.4</td>
      <td>15 Jul 2026</td>
      <td>
        <p><b>📱 Devices</b><br/>
          ✅ Fixed NEC MP700 touch</p>
      </td>
    </tr>
    <tr>
      <td>v6.3</td>
      <td>15 Jul 2026</td>
      <td>
        <p><b>📱 Devices</b><br/>
          ✅ Fixed a crash when PC Card was re-inserted too fast in DevEmu boards</p>
        <p><b>💿 Emulator</b><br/>
          ✅ UI updates<br/>
          ✅ Dev only: SDK/Build tools reorganization</p>
        <p><b>🚀 Launcher</b><br/>
          🆕 Added multiple bundle repositories configuration</p>
        <p><b>✨ Guest Additions</b><br/>
          ✅ Fixed IMGFS ROMs regression introduced in v6.0<br/>
          ✅ Software rendering is fully removed and replaced with hardware rendering. Microsoft dependencies dropped.<br/>
          ✅ Fixed incorrect hardware communication approach for display and shared storage. Now rendering and shared storage is stable.</p>
      </td>
    </tr>
    <tr>
      <td>v6.2</td>
      <td>12 Jul 2026</td>
      <td>
        <p><b>💿 Emulator</b><br/>
          🆕 cerf.exe now runs on Windows XP and newer - one binary, XP through 11<br/>
          ✅ Fixed a crash on startup on every Windows older than 10 1809 (dark-mode init)</p>
        <p><b>🚀 Launcher</b><br/>
          🆕 launcher_vista.exe: the launcher for Windows Vista and newer</p>
      </td>
    </tr>
    <tr>
      <td colspan="3"><b>Previous versions</b> - see the <a href="https://cerf.cx/changelog/">full changelog</a>.</td>
    </tr>
  </tbody>
</table>

## Known Issues

See [launcher's boards details database](launcher/supported_devices.py) for per-board issues.

## Claude Development Environment

> [!CAUTION]
> **DO NOT USE CERF CODEBASE AS REFERENCE FOR SoCs, BOARDS, PERIPHERALS** - AI WRITTEN CODE CAN'T BE TRUSTED!

Built with [Claude](https://claude.ai) via [Claude Code](https://docs.anthropic.com/en/docs/claude-code). Not production-grade.

---

CERF ships a Claude Code-based development environment for working on the emulator - including bringing up brand-new boards from their ROMs. Launch it from the repo root with:

```
run_claude.cmd
```

It runs Claude Code with a custom system prompt that injects the **entire project documentation** (`CLAUDE.md` plus every `agent_docs/` reference page) into every agent, so each session starts fully briefed on the project's rules, architecture, and subsystems - no "please read the docs first" needed.

The environment provides the **`/start-board-implementation`** skill: drop your ROM into `bundled/devices/` (or just point the agent at it) and run the skill. The agent identifies the board and SoC straight from the ROM, checks what CERF already supports, estimates the effort, and - on your go-ahead - starts the bring-up with a cross-session tracking document. So you can literally drop in your ROM and start the procedure of bringing it up.

> [!WARNING]
> The dev environment runs Claude in skip-permissions mode - it can execute anything on your machine without prompting. It also force-kills its own Claude instance, and **any** `clangd.exe`, that leaks memory past a threshold. The first launch shows a one-time explanation; press Enter to acknowledge it.

## License

[MIT](LICENSE). Third-party components and studied references are listed in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

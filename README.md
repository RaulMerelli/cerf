<img src="docs/github_band.svg" width="100%"/>

<div align="center">
  <b>CE Runtime Foundation</b> (aka <i>CERF</I>) - <b>Universal Windows CE emulator</b><br/>
  <b><a href="https://cerf.cx">cerf.cx</a></b> - read more information about the project  
</div><br/>

<div align="center">
  <a href="https://discord.gg/QREE9Y2v2d"><img src="https://img.shields.io/badge/Discord-join%20the%20server-5865F2?logo=discord&amp;logoColor=white" alt="Discord"/></a> <a href="https://www.patreon.com/dz3n"><img src="https://img.shields.io/badge/Patreon-support-FF424D?logo=patreon&amp;logoColor=white" alt="Patreon"/></a>
</div>

<br/>

> [!WARNING]
> **Beta stage.** CERF is a one-person open source project. Expect bugs and
> breaking changes.

> [!CAUTION]
> **CERF 6.8 is temporary broken.** We remove third-party licensed
> code from the emulator core and write new code from primary documentation
> The current version still does not correspond to CERF 6.7 level of stability and performance.

## Downloads

To use the newest features, download the WIP build (6.8) from the artifacts [![build](https://github.com/gweslab/cerf/actions/workflows/build.yml/badge.svg)](https://github.com/gweslab/cerf/actions/workflows/build.yml). For a stable version, go to the [latest release](https://github.com/gweslab/cerf/releases/latest).

Run **`launcher.exe`** and select a device. The launcher downloads the ROM bundle and boots it. The [articles](https://cerf.cx/articles/command-line/) show how to run `cerf.exe --device=...` directly, and describe its command line and its logs.

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
      <td align="center"><img src="launcher/assets/icons/badge_mips.png" align="middle" title="MIPS" alt="MIPS"/><br/><b>NEC VR4122</b><br/><sub>MIPS III</sub></td>
      <td>
        <img src="cerf/assets/icons_sources/board.svg" width="16" height="16" title="PDA" alt="PDA"/> <b>Casio Cassiopeia EM-500</b> <code>casio_cassiopeia_em500</code><br/>
        Pocket PC 2000
      </td>
      <td><img src="cerf/assets/icons_sources/display.svg" width="32" height="32" title="Display" alt="Display"/> <img src="cerf/assets/icons_sources/stylus.svg" width="32" height="32" title="Touch" alt="Touch"/> <img src="cerf/assets/icons_sources/ga_autoresize.svg" width="32" height="32" title="Guest Additions" alt="Guest Additions"/> <img src="cerf/assets/icons_sources/speaker_active.svg" width="32" height="32" title="Sound" alt="Sound"/></td>
    </tr>
    <tr>
      <td align="center"><img src="launcher/assets/icons/badge_mips.png" align="middle" title="MIPS" alt="MIPS"/><br/><b>NEC VR4121</b><br/><sub>MIPS III</sub></td>
      <td>
        <img src="cerf/assets/icons_sources/board.svg" width="16" height="16" title="PDA" alt="PDA"/> <b>Casio Toricomail / Message-Cam / Pocket PostPet</b> <code>casio_toricomail</code><br/>
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

A ROM boots only if **CERF implements that exact board**. A matching SoC is not sufficient.

**The board is on the supported list.** The [articles](https://cerf.cx/articles/own-rom/) show how to boot your own dump.

**The board is not on the supported list.** A new board is a code contribution. It needs C++ for the memory map of the board, for each peripheral that the drivers use, and for the quirks of the SoC. The code must agree with datasheets, BSP sources and reverse engineering, at the quality level of the current tree. A new board is not a change to a configuration file - that's not that simple.

> [!IMPORTANT]
> **CERF does not accept ROM submissions or requests for new boards.** Send a contribution, or use a board that CERF supports. The project also adds a board on its own sometimes, when the board is important for historical preservation, or interesting.

## Building

CERF requires Visual Studio 2026 with the C++ desktop development workload.

> [!NOTE]
> **The first build on a new machine takes more than one hour.** vcpkg compiles the dependencies from source before CERF links. This occurs one time on each machine. Later builds use the cached `vcpkg_installed/` tree and are complete in a few minutes. Do not stop the first build.

Configure the clone (one time on each machine):

```
setup.cmd
```

This script initializes the submodules. It points git at the tracked hooks of the
repo (`core.hooksPath` = `.githooks`). Git does not clone the hook configuration,
so the hooks do nothing in a new clone until you run this script. The script also
reports each missing prerequisite (the Python launcher, the vcpkg MSBuild
integration). You can run it again at any time, because it is idempotent.
`setup.cmd -Check` reports the status and changes nothing.

Build with the helper script:

```
powershell -ExecutionPolicy Bypass -File build.ps1
```

Or run msbuild directly:

```
msbuild cerf.sln /p:Configuration=Release /p:Platform=Win32
```

### Building the CE-side binaries (optional)

`ce_apps/` holds the Windows CE binaries that CERF ships, and the Guest Additions
display driver. To build them, you need a CE toolchain and a CE SDK. `cerf.exe`
does **not** need them. If you work on the emulator core, the boards, the SoCs,
the JIT or the host UI, use the prebuilt binaries.

To build them, install eMbedded Visual C++ 4.0 (a free Microsoft download from the
Microsoft archive). Then run one script. The full instructions are in
**[docs/ce_apps_setup.md](docs/ce_apps_setup.md)**.

`setup.cmd -Check` reports whether the CE toolchain is present.

CERF builds the website from `docs/website/`. The command `python tools/build_site.py --serve` runs the website on your machine with live reload.

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
      <td>v6.8</td>
      <td>TBA</td>
      <td>
        <p><b>📱 Devices</b><br/>
          🆕 Casio Toricomail: bezel touch buttons</p>
        <p><b>💿 Emulator</b><br/>
          ✅ ARM JIT and JIT core full rewrite. ARM JIT/peripherals optimizations. Expect new issues.</p>
        <p><b>🚀 Launcher</b><br/>
          🆕 Copyright removal dialog listing each bundle repository&#x27;s abuse contact, reachable from the Download ROMs window and the download confirmation<br/>
          🆕 GitHub issues window (Bugs&amp;Requests)<br/>
          ✅ Installed devices are listed and launchable immediately at startup instead of waiting for the bundle catalog on a slow or absent connection<br/>
          ✅ Toolbar buttons that no longer fit a narrow window move into a chevron menu instead of being cut off</p>
      </td>
    </tr>
    <tr>
      <td>v6.7</td>
      <td>23 Jul 2026</td>
      <td>
        <p><b>📱 Devices</b><br/>
          🆕 Casio Cassiopeia EM-500 support (bare bones)<br/>
          ✅ Sharp Mobilon HC-4100: fixed suspend crash</p>
        <p><b>💿 Emulator</b><br/>
          🆕 Discord Rich Presence - shows the current device and OS in your Discord profile<br/>
          🆕 UI updates<br/>
          ✅ Fixed framebuffer not relatching on suspend/resume<br/>
          ✅ Fixed 100% CPU usage and UI deadlocks on Windows XP on non-framebuffer tabs</p>
        <p><b>🚀 Launcher</b><br/>
          🆕 UI refresh<br/>
          ✅ Metadata-only remote updates no longer re-download the entire ROM<br/>
          ✅ Fixed the command-line interface producing no output<br/>
          ✅ Configuration, updates and removal are now blocked while a device is running<br/>
          ✅ Single click on a device preview now launches it everywhere<br/>
          ✅ Merged the two launcher builds into a single Windows Vista+ executable<br/>
          ❌ Removed the redundant soc_family and board_name fields from cerf.json</p>
        <p><b>💾 CE Apps</b><br/>
          ✅ CerfDemo: UI and performance improvements</p>
        <p><b>✨ Guest Additions</b><br/>
          🆕 High refresh rate support - use Windows CE with 240 hz display! (Or whatever Hz you have). Yes, this should be taken LITERALLY. WinCE WILL render 240 fps on your 240 hz monitor. The guest video mode and host window scanout follow the host monitor&#x27;s refresh rate<br/>
          🆕 --screen-refresh-rate flag to set the refresh rate manually<br/>
          🆕 Touch-calibration helper - offers to switch to the stock input device when the guest opens a calibration screen, and switches back afterwards<br/>
          🆕 Color scheme overrides - colorize grayscale devices with a forced system color scheme<br/>
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
      <td colspan="3"><b>Previous versions</b> - see the <a href="https://cerf.cx/changelog/">full changelog</a>.</td>
    </tr>
  </tbody>
</table>

## Known Issues

For the issues of each board, see the [board database of the launcher](launcher/supported_devices.py).

## Claude Development Environment

The project is primarily built with help of [Claude](https://claude.ai) and [Claude Code](https://docs.anthropic.com/en/docs/claude-code).

> [!CAUTION]
> AI written code (and well - the human written code too) might include mistakes a developer did not notice. Be careful when using it as a reference for peripherals and other hardware level systems.

---

CERF includes a development environment that uses Claude Code. You can work on the emulator with it, and you can add new boards from their ROMs. Run it from the root of the repo:

```
run_claude.cmd
```

This environment runs Claude Code with a custom system prompt. The prompt puts the **full project documentation** into each agent (`CLAUDE.md` and each reference page in `agent_docs/`). Thus each session starts with the rules, the architecture and the subsystems of the project. You do not tell the agent to read the documentation first.

The environment gives you the **`/start-board-implementation`** skill. Put your ROM into `bundled/devices/`, or give the agent the path to it. Then run the skill. The agent identifies the board and the SoC from the ROM. It then examines what CERF supports and estimates the work. If you agree, the agent starts the work and writes a tracking document that stays between sessions.

> [!WARNING]
> The development environment runs Claude in skip-permissions mode. Claude can run any command on your machine, and it does not ask you first. The environment also stops its own Claude instance, and **any** `clangd.exe`, that uses more memory than a limit. At the first start, it shows an explanation one time. Press Enter to accept it.

## License

[MIT](LICENSE). Third-party components and studied references are listed in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

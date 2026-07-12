# Running your own ROM

CERF emulates a *whole device* - the SoC, the board, the memory map and every peripheral the ROM's
drivers touch. A ROM boots when **that board is implemented in CERF**. The same chip on another
board has different RAM and flash addresses, a different display controller and different wiring,
so a matching SoC is not enough.

In practice: a dump boots if its board is on the [supported list](../boards.md). A ROM found on the
internet for some other device will not.

## A dump of a board CERF already supports

A different region or revision of a supported device - say another Jornada 720 ROM - can be dropped
in by hand.

1. Create a folder under `devices/` (next to `cerf.exe`), e.g. `devices/mydump/`.
2. Put the ROM image in it, e.g. `mykernel.nb0`.
3. Add a `cerf.json` naming the board and the ROM:

    ```json
    {
      "board": { "id": "jornada_720" },
      "rom":   { "primary": "mykernel.nb0" }
    }
    ```

4. Run `cerf.exe --device=mydump`.

The board id is your device's - `cerf.exe --help` lists them all. Both fields can also be given on
the command line instead:

```
cerf.exe --device=mydump --board-id=jornada_720 --rom-primary=mykernel.nb0
```

!!! note "The file can be the OEM one, as shipped"

    `rom.primary` does not have to be a plain flat image. CERF unwraps the containers these ROMs
    actually ship in - multi-XIP images, vendor firmware packages - and can serve a whole-flash or
    NAND dump through the emulated storage controller, letting the device's own boot path find the
    image inside it.

## A board CERF does not support

That is emulator development, not configuration. The board's memory map and every peripheral its
drivers touch have to be implemented in C++ - there is no ROM you can drop in, and no setting that
makes an unimplemented board boot.

The work is done in the [repository](https://github.com/gweslab/cerf), and the bar is the quality
of what CERF already ships: a correct memory map, real per-board peripherals, and behaviour grounded
in datasheets, BSP sources and reverse engineering - not values that happen to work.

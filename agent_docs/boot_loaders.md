# What to do with boot loaders in CERF?

The boot loader part is the boot loader of the separate board. It is usually an original OEM blob: EBOOT.BIN, ZBOOT.BIN, SBOOT.BIN, and more. It is either another XIP that contains a parent nk.exe, or other arbitrary machine code that does the pre-kernel setup.

For most boards CERF skips the boot loader part fully and boots the XIP blob itself (at the moment of June 2026). The Windows CE images are usually happy with this. They are greatly decoupled from boot loaders. The boot loaders themselves do barely anything. Zune 30 is a good candidate. From what it seems, it has NK.BIN on the HDD.

The boot loader most likely spins the HDD, loads NK.BIN, and starts the kernel. CERF skips this part fully, because CERF had only the NK.BIN extracted from the OS update tool, and we did not know the architecture when we built the emulation layer for Zune 30. The OS itself needs the HDD with OsPartition anyway. Therefore CERF synthesizes a blank HDD with that partition. That HDD has no NK.BIN, because the OS itself does not need to read it. This is the good and beautiful case: CERF skipped the boot loader entirely and booted the OS from an unusual place, and it all works very well.

This is how it works in a perfect world! But look at the NEC MobilePro 900 Series. It has two known ROMs (at least to me). One is the CE .NET upgrade package with a new boot loader (SABOOT.NB0). The other is what I assume is the original HPC2000 re-flashing utility, without a boot loader. The situation here is exactly where the pattern fails: the boot loader does not do anything too special, but it writes a struct into a specific address. This struct is a display configuration table which has 3 resolutions, and only one of them works (640x240). The OS will not boot if it is 0 (640x480).

So we first found the exact failure on CE4. Then we examined the boot loader code and found what it writes. And it worked. But not for the CE3 version. So we examined the same ddi.dll, at the same place, and found that the chain is identical. It reads identical data, but it reads the data from a different address. This is the boot loader that does the pre-setup, and worse - this is a boot loader fully coupled with the specific ROM.

So at this point CERF can boot the boot loader instead. But the situation is still unclear: CE3 has no boot loader publicly available (at least from what I know). The solution here was to rely on the absence of other ROMs, and to compare the nk.exe subsystem version to write the same data at two places.

But this is still a perfect world picture: nobody denies a boot loader the right to do more work. In more obscure or complex systems the boot loader does so much that it is unavoidable.

The lesson here is that CERF omits the boot loader only for the ROMs which are fully happy with this. If the boot loader is ROM-blob agnostic, CERF can model it for some ROMs. If the boot loader does too much complex work and is very ROM-image coupled, then CERF has no choice but to model the hardware faithfully and run the boot loader on it.

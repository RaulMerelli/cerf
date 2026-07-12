# Connecting to ActiveSync

This guide connects a CERF guest to **ActiveSync 3.8** running in a **Windows 98** VMware virtual
machine, as a PC Link partnership over an emulated serial link. Windows CE 1.0 (which uses
"Microsoft Windows CE Services" instead), other ActiveSync versions, and connecting over a network
adapter are all believed to work but are not officially tested yet.

The connection is built in four stages:

1. [Install and configure com0com](#1-com0com) - a virtual COM port pair on the host.
2. [Set up the Windows 98 VM](#2-windows-98-vmware-vm) - and hand it one end of that pair.
3. [Configure CERF](#3-cerf-side-serial-forwarding) - and hand it the other end.
4. [Finish the connection](#4-finishing-the-connection) - wire the guest's serial port to
   ActiveSync and link up.

If your target is not a virtual machine - a host-installed ActiveSync, or a physical COM port - you
can skip the stages that do not apply to you.

!!! warning "Expect rough edges"

    The serial port forwarder, like everything in CERF, is early, and not every path has been
    proven to work end to end. Be ready for any outcome.

## 1. com0com

Download and install com0com from [com0com.sourceforge.net](https://com0com.sourceforge.net/).

Open the configurator (`setupg.exe`) and create a virtual COM pair. This guide follows the example
pair **COM9 &harr; COM10**.

![com0com configurator with a COM9 to COM10 pair](/assets/guides/activesync/com0com_setup.png)

## 2. Windows 98 VMware VM

Give the VM one end of the pair - **COM10** in this example. Follow the setup in the screenshot:

![VMware serial port set to COM10](/assets/guides/activesync/vmware_w98_config.png)

Or add these lines to the VM's `.vmx` file directly:

```
serial0.fileName = "COM10"
serial0.present = "TRUE"
serial0.tryNoRxLoss = "TRUE"
serial0.yieldOnMsrRead = "TRUE"
```

Boot the VM, install the ActiveSync version you want (this guide uses 3.8), then move on to the
CERF side.

## 3. CERF-side serial forwarding

Serial can reach the guest two ways, depending on whether the board has a built-in serial port and
whether CERF emulates it. Pick one:

- **[Serial forwarder via PC Card](#serial-forwarder-via-pc-card)** - for a guest with no built-in
  serial port, or where CERF does not emulate one. The involved path.
- **[Built-in serial port](#built-in-serial-port)** - when the board has a working serial port. The
  simple path.

Either way, you end at [Finishing the connection](#4-finishing-the-connection).

### Serial forwarder via PC Card

Use this when the guest has no built-in serial port, or CERF does not emulate one.

Launch the device and wait for the desktop.

**1.** From the status bar's PC Card widget (or the Actions menu), pick a free PCMCIA slot and
insert a **Serial Port Forwarder**, pointing it at the *other* end of your com0com pair. Windows 98
took COM10, so CERF takes **COM9**.

![Inserting the Serial Port Forwarder on COM9](/assets/guides/activesync/guest-pc-card-1.png)

**2.** Open **Start &rarr; Programs &rarr; Communications &rarr; Remote Networking**.

![Remote Networking in the Start menu](/assets/guides/activesync/guest-pc-card-2.png)

**3.** Open **Make New Connection**, choose **Direct connection**, and click **Next**.

![Make New Connection, Direct connection selected](/assets/guides/activesync/guest-pc-card-3.png)

**4.** Check whether the device list contains `CERF-Serial_Forwarder` or `Serial Cable on PC Card`.

![The device list in the Direct connection wizard](/assets/guides/activesync/guest-pc-card-4.png)

=== "The forwarder is listed"

    The guest recognised the card correctly - it is ready to use. Continue to
    [Finishing the connection](#4-finishing-the-connection).

=== "The forwarder is not listed"

    The guest has most likely filed it under dial-up modems instead. Continue to
    [Substituting the modem identity](#substituting-the-modem-identity) below.

#### Substituting the modem identity

First, confirm the card really was identified as a modem.

**1.** Click **Back**, this time choose **Dial-up connection**, and click **Next**.

![The Dial-up connection wizard](/assets/guides/activesync/guest-pc-card-5.png)

**2.** Look at the device list. Is `CERF-Serial_Forwarder` or `Serial Cable on PC Card` in it?

![The device list in the Dial-up wizard](/assets/guides/activesync/guest-pc-card-6.png)

!!! warning

    If the forwarder does not appear here either, this approach is almost certainly a dead end. You
    can read on, but the chance the PC Card path will work is close to zero.

**3.** Copy `pccardserial.exe` into the guest and run it. It ships in your CERF directory under
`ce_apps\<cpu>\pccardserial.exe` - open `ce_apps\` and pick the folder that matches the guest's
CPU, e.g. `ce_apps\mips1\pccardserial.exe`. Move it in by whatever means is convenient - for
example, insert a CompactFlash card in another slot, or eject the forwarder, insert a CF card, copy
the EXE to local storage, and re-insert the forwarder.

**4.** When it runs, does it offer to substitute the identity?

![pccardserial.exe offering to register the card as a serial cable](/assets/guides/activesync/guest-pc-card-7.png)

If it does, accept.

!!! warning

    If it offers nothing, the PC Card path is a dead end - there is no other route through it.

**5.** **Eject** the forwarder, then **insert** it into the slot again.

**6.** Return to the **Make New Connection** wizard, choose **Direct connection** again, and check
the device list. If it now includes `CERF-Serial_Forwarder` or `Serial Cable on PC Card`, that is
the win - select it and create the connection.

![New connection wizard proposes Serial Cable on PC Card](/assets/guides/activesync/guest-pc-available-direct.png)

**7.** Do **not** connect to it yet - that is the wrong action in the wrong place. Instead, open
**Control Panel &rarr; Communications Properties**, and go to the **PC Connection** tab.

![Communications Properties, PC Connection tab](/assets/guides/activesync/guest-pc-card-8.png)

**8.** Under **Connect using:**, which is probably still set to the built-in communications
peripheral, click **Change**, and pick the connection you just created. The default name is kept
here - literally **My Connection**.

![Choosing My Connection under Connect using](/assets/guides/activesync/guest-pc-card-9.png)

**9.** Click through the **OK** buttons to save and close the Control Panel applets. **Done** - the
PC Card forwarder is registered and the guest now treats it correctly. Continue to
[Finishing the connection](#4-finishing-the-connection).

### Built-in serial port

If the board has a built-in serial port, there is no PC Card and no identity to fix - just point
the port at your host COM.

Right-click the built-in serial icon in the status bar (or use the Actions menu) and choose
**Insert card &rarr; Serial Port Forwarder**, then pick the host COM port - the free end of your
com0com pair, **COM9** here, since COM10 went to the VM.

![Attaching the Serial Port Forwarder to the built-in serial port](/assets/guides/activesync/built-in-selection.png)

Then go to [Finishing the connection](#4-finishing-the-connection).

## 4. Finishing the connection

**1.** If you have not already, on the guest open **Control Panel &rarr; Communications Properties**
and make sure the correct serial port is selected there. With a built-in serial port this is
usually already set.

**2.** With the wiring in place, finish the ActiveSync side. Open ActiveSync 3.8, and from the
**File** menu choose **Connection Settings...**

![ActiveSync 3.8 File menu, Connection Settings](/assets/guides/activesync/activesync-config-1.png)

**3.** Enable **Allow serial cable or infrared connection to this COM port:** and pick the
guest-side COM port below - usually **COM1**. This is the guest's own port, not the COM9/COM10 pair
you set up on the host.

![Connection Settings with the serial COM port enabled](/assets/guides/activesync/activesync-config-2.png)

**4.** Watch the guest right after this - the serial connection window will very likely appear on
its own.

If it does not, open **Start &rarr; Programs &rarr; Communications &rarr; PC Link** to start the
connection yourself.

![PC Link in the Start menu](/assets/guides/activesync/guest-pc-link.png)

**5.** Done. The guest should now be connected, and ActiveSync 3.8 should offer to set up a
**partnership** - from here you are in ordinary ActiveSync territory.

![ActiveSync connected, offering a partnership](/assets/guides/activesync/guest-connected.png)

!!! warning

    If it does not connect, that is the luck of the draw - the fault could be on the emulation side
    or in the host wiring. The chain is fragile.

# Segger J-Link

![Image of a JLink EDU Mini](https://www.sparkfun.com/media/.renditions/wysiwyg/Documentation/31107-SEGGER-J-Link-EDU-Mini-Feature.jpg)

> Educational institutions can get the JLink EDU Mini. More information [here](https://www.segger.com/products/debug-probes/j-link/models/j-link-edu-mini/)

The Segger J-Link is pretty much the standard debug probe type used for
development in ARM-Cortex based platforms. There are different versions that
have different tradeoffs like speed, size, features, output connector, etc.

## Setup to use with zephyr

While J-Link can be used with openOCD, it's better to use the official tools
from Segger. Depending on the distro you are using, there might be a package
available in your package manager:

* Ubuntu:
  ```bash
  sudo apt install jlink
  ```
* Other: get the installer from [here](https://www.segger.com/downloads/jlink/).

## Usage

### Verify it's there

Make sure after the install that the J-Link tools can be found in the path:

```Shell
JLinkExe
```

This should output something like

```Shell
SEGGER J-Link Commander V9.48 (Compiled Jun  3 2026 14:24:02)
DLL version V9.48, compiled Jun  3 2026 14:23:19

Connecting to J-Link ...O.K.
Firmware: J-Link EDU Mini V2 compiled May 26 2026 15:18:20
Hardware version: V2.00
J-Link uptime (since boot): 0d 00h 09m 39s
S/N: 802010166
License(s): FlashBP,GDB
USB speed mode: Full speed (12 MBit/s)
VTref=3.341V


Type "connect" to establish a target connection, '?' for help
J-Link>
```

Execute the `exit` command of press `Ctrl+C` to close the program

### Verify it can control a target

```Shell
JLinkExe -device nRF52832_xxAA -if SWD -speed 4000 -autoconnect 1
```

> If you are connecting to a Mingle Midge v2, change the device to `nrf52840_xxAA`

This should show up something like:

```Shell
SEGGER J-Link Commander V9.48 (Compiled Jun  3 2026 14:24:02)
DLL version V9.48, compiled Jun  3 2026 14:23:19

Connecting to J-Link ...O.K.
Firmware: J-Link EDU Mini V2 compiled May 26 2026 15:18:20
Hardware version: V2.00
J-Link uptime (since boot): 0d 00h 13m 02s
S/N: 802010166
License(s): FlashBP,GDB
USB speed mode: Full speed (12 MBit/s)
VTref=3.337V
Device "NRF52832_XXAA" selected.


Connecting to target via SWD
InitTarget() start
InitTarget() end - Took 2.28ms
Found SW-DP with ID 0x2BA01477
DPIDR: 0x2BA01477
CoreSight SoC-400 or earlier
Scanning AP map to find all available APs
AP[2]: Stopped AP scan as end of AP map has been reached
AP[0]: AHB-AP (IDR: 0x24770011, ADDR: 0x00000000)
AP[1]: JTAG-AP (IDR: 0x02880000, ADDR: 0x01000000)
Iterating through AP map to find AHB-AP to use
AP[0]: Core found
AP[0]: AHB-AP ROM base: 0xE00FF000
CPUID register: 0x410FC241. Implementer code: 0x41 (ARM)
Found Cortex-M4 r0p1, Little endian.
FPUnit: 6 code (BP) slots and 2 literal slots
CoreSight components:
ROMTbl[0] @ E00FF000
[0][0]: E000E000 CID B105E00D PID 000BB00C SCS-M7
[0][1]: E0001000 CID B105E00D PID 003BB002 DWT
[0][2]: E0002000 CID B105E00D PID 002BB003 FPB
[0][3]: E0000000 CID B105E00D PID 003BB001 ITM
[0][4]: E0040000 CID B105900D PID 000BB9A1 TPIU
[0][5]: E0041000 CID B105900D PID 000BB925 ETM
Memory zones:
  Zone: "Default" Description: Default access mode
Cortex-M4 identified.
J-Link>
```

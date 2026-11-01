# X Scope

Open firmware for FNIRSI oscilloscopes.

## FNIRSI-1014D

Firmware for the FNIRSI 1014D benchtop oscilloscope (Allwinner F1C100s, stock FPGA 0x1432).
It runs from the internal SD card; the original firmware stays in the SPI flash and remains
available from the loader menu.

| Folder | Contents |
|---|---|
| `FNIRSI-1014D/firmware` | Scope firmware source |
| `FNIRSI-1014D/loader` | SD card loader source. Hold a key at power on: F1 X Scope, F2 original firmware, F3 FEL (USB) mode |
| `FNIRSI-1014D/tools` | SD card image packer |
| `FNIRSI-1014D/installer` | Windows installer source |
| `X Scope - Application` | X Scope Remote, the Windows application for live view and control over USB |

### Features

- Two channels, auto / normal / single trigger, roll mode down to 50 s/div
- Six measurement slots on F1 to F6, time and voltage cursors with optional cursor lock
- Channel menu: probe x0.5 to x1000, DC/AC, volt or ampere, invert, FFT
- Display & analysis: trace styles (linear, dots, peak, pro, sin(x)/x), averaging, grid on/off,
  FFT per channel, math (A+B, A-B, AxB, A/B), software trigger hold off, eight reference waveforms
- Utilities: input (gain) calibration, CSV export to the SD card, USB mass storage or serial,
  auto setup old/new, diagnostics with live timing
- Remote control over USB: starts in USB serial mode, used by X Scope Remote
  (see [X Scope - Application](X%20Scope%20-%20Application))
- Base (zero) calibration, X-Y mode, picture and waveform save/browse, USB disk access
- Data cache enabled (MMU, write-through), about 40 screen updates per second

The signal generator menu is present, but the stock FPGA has no known generator command, so it
produces no output on the stock FPGA.

### Install on Windows

1. Download `X-Scope-Installer.exe` from the releases page.
2. Connect the scope by USB and open its USB disk mode: in the original firmware the USB
   connection option of its menu, in X Scope Utilities, USB mode, Mass storage, then MENU,
   USB export.
3. Start the installer (it asks for administrator rights), select the scope and press Install.

On the first install the card is repartitioned (its partition must start at 1 MiB), which erases
the files on the card; the installer offers a full backup of the card first. Later updates keep
the partition, files and settings. After writing, leave the USB mode (in the original firmware
switch the scope off with the power button), then switch the scope off and on.

To go back to the original firmware, hold a key at power on and choose F2, or restore the backup.

### Build

Needs the GNU Arm toolchain (`arm-none-eabi-gcc`), GNU make and Python 3:

```
cd FNIRSI-1014D
make
```

`build/x-scope-1014d.bin` is the SD card image (written at 8 KiB), `build/x-scope-1014d-ram.bin`
the firmware alone for loading over USB in FEL mode. The installer is built on Windows with
`installer\build.cmd` after `make`.

## License

GPLv3, see [LICENSE](LICENSE). X Scope is a modified version (2026) of GPLv3 licensed open
firmware for FNIRSI oscilloscopes. FatFs is copyright ChaN under its own license, included in
the source files.

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

### Features

- Two channels, auto / normal / single trigger, roll mode down to 50 s/div
- Six measurement slots on F1 to F6, time and voltage cursors with optional cursor lock
- Channel menu: probe x0.5 to x1000, DC/AC, volt or ampere, invert, FFT
- Display & analysis: trace styles (linear, dots, peak, pro, sin(x)/x), averaging, grid on/off,
  FFT per channel, math (A+B, A-B, AxB, A/B), software trigger hold off, eight reference waveforms
- Utilities: input (gain) calibration, CSV export to the SD card, USB mass storage or serial,
  auto setup old/new, diagnostics with live timing
- Base (zero) calibration, X-Y mode, picture and waveform save/browse, USB disk access
- Data cache enabled (MMU, write-through), about 40 screen updates per second

The signal generator menu is present, but the stock FPGA has no known generator command, so it
produces no output on the stock FPGA.

## License

GPLv3, see [LICENSE](LICENSE). X Scope is a modified version (2026) of GPLv3 licensed open
firmware for FNIRSI oscilloscopes. FatFs is copyright ChaN under its own license, included in
the source files.

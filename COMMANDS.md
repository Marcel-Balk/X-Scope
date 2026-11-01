# X Scope remote commands

X Scope (FNIRSI 1014D) takes commands over its USB serial port. The scope starts in USB serial
mode; on Windows it shows up as a COM port (any baud rate, 8N1).

## Protocol

- Send one line per request, ended by CR, LF or CR LF. Keywords are case insensitive.
- Several commands can share a line, separated by `;`. Each command starts from the root, so
  write the full header every time (`CHAN1:SCAL 1;CHAN1:POS 0`).
- Every command answers with one of:
  - value lines (queries, ending in `?`)
  - `OK` (settings)
  - `#ERR <code>,<text>`
- The line is closed with `#END`. Wait for `#END` before you send the next line. A line that
  arrives while the previous one is still being handled gets `#ERR busy`.
- A command that fails changes nothing. Every value is checked against the scope's own tables
  first. Use exact table values: 1-2-5 steps for V/div and time/div, the listed probe ratios
  and baud rates.
- Numbers are plain or scientific (`0.005`, `5e-3`). An SI multiplier and a unit may follow:
  `5m`, `500us`, `10k`, `2.5V`. As in SCPI, `M` means milli.
- Numbers come back as `d.dddddE+xx`. A measurement that this capture cannot produce is
  `9.91E+37`, the SCPI "not a number". A flat line, for example, has no frequency.
- Settings change only on the live trace screen. An open menu is closed first, the same way
  its own key would close it. While a saved picture or waveform is on the screen, settings
  commands answer `#ERR -221`.
- Changes are kept like front panel changes: they are saved when the scope is switched off
  with its power key.

Short forms are the upper case part of each keyword: `CHANnel1` is `CHAN1` or `CHANNEL1`,
`SCALe` is `SCAL` or `SCALE`.

## Error codes

| Code | Meaning |
|---|---|
| -102 | Syntax error (not a command header) |
| -104 | Wrong kind of value (for example a word where a number belongs) |
| -112 | Command longer than 127 characters |
| -113 | Unknown command |
| -114 | Number in the header out of range (`CHAN3`, `SLOT7`) |
| -221 | Not possible now (a saved file is on the screen) |
| -222 | Value out of range or not one of the allowed steps |
| -241 | Not available in this firmware |

`SYSTem:ERRor?` returns the last error (`<code>,"<text>"`) and clears it.

## General

| Command | Description |
|---|---|
| `*IDN?` | `X Scope,FNIRSI 1014D,0,<version>` |
| `*OPC?` | `1` |
| `*CLS`, `*WAI` | Clear the error / no operation |
| `SYSTem:ERRor?` | Last error |
| `SYSTem:SETup?` | All settings as a list of commands, one per line. Sent back in this order, they recreate the setup (X Scope Remote saves profiles this way) |
| `RUN`, `STOP` | Start or stop acquisition |
| `SINGle` | Single trigger mode and arm one capture |
| `RSTate?` | `RUN` or `STOP` |
| `AUToscale` | Same as the AUTO key |

## Channels (`<n>` is 1 or 2)

| Command | Values |
|---|---|
| `CHANnel<n>:DISPlay` | `ON`, `OFF` |
| `CHANnel<n>:COUPling` | `AC`, `DC` |
| `CHANnel<n>:PROBe` | `0.5`, `1`, `10`, `20`, `50`, `100`, `1000` |
| `CHANnel<n>:SCALe` | Volts per division at the probe tip, one of the seven steps of the current probe (x1: 5, 2.5, 1, 0.5, 0.2, 0.1, 0.05; x10: 50 down to 0.5) |
| `CHANnel<n>:POSition` | Position of the trace's 0 V line in divisions from the screen center, -4 to 4 |
| `CHANnel<n>:OFFSet` | The same position in volts (divisions x V/div) |
| `CHANnel<n>:UNIT` | `V`, `A` (current probe display) |
| `CHANnel<n>:INVert` | `ON`, `OFF` |
| `CHANnel<n>:FFT` | `ON`, `OFF` (spectrum in place of the trace) |

Every setting can also be queried: add `?` (`CHAN1:SCAL?`).

## Time base and trigger

| Command | Values |
|---|---|
| `TIMebase:SCALe` | Seconds per division, 5n to 200m (sweep), 500m to 50 (roll) |
| `TIMebase:POSition` | Trigger point from the screen center in seconds (positive = right). Values past the record are limited to it |
| `TRIGger:MODE` | `AUTO`, `NORMal`, `SINGle` |
| `TRIGger:SLOPe` (also `TRIGger:EDGE:SLOPe`) | `RISing`, `FALLing` (also `POSitive`, `NEGative`) |
| `TRIGger:SOURce` | `CHAN1`, `CHAN2` |
| `TRIGger:LEVel` | Volts. Limited to the ADC range of the trigger channel |
| `TRIGger:FIFTy` | Level to 50 % of the signal (no query) |
| `TRIGger:HOLDoff` | `0`, `10m`, `20m`, `50m`, `100m`, `200m`, `500m`, `1`, `2` seconds |

## Display, acquisition and math

| Command | Values |
|---|---|
| `DISPlay:XY` | `ON`, `OFF` |
| `DISPlay:GRID` | `ON`, `OFF` |
| `DISPlay:TRACe` | `LINear`, `DOTS`, `PEAK`, `PRO`, `SINC` |
| `DISPlay:PERSistence` | Not available (`#ERR -241`) |
| `ACQuire:AVERage` | `ON`, `OFF` |
| `MATH:MODE` | `OFF`, `ADD`, `SUBtract`, `MULtiply`, `DIVide` |
| `MATH:SOURce1`, `MATH:SOURce2` | `CHAN1`, `CHAN2`, `REF1`, `REF2` |

## Measurements

The scope measures every capture on the raw samples. The values below come from that
analysis. They can differ slightly from the F1 to F6 readouts, which the original code computes
in its own way.

| Name | Unit | Description |
|---|---|---|
| `VMAX`, `VMIN` | V | Highest and lowest sample |
| `VAVG` | V | Mean |
| `VRMS` | V | True RMS, AC + DC |
| `VRMSAC` | V | True RMS of the AC part (standard deviation) |
| `VPP` | V | Peak to peak |
| `VP` | V | Largest of \|Vmax\| and \|Vmin\| |
| `VTOP`, `VBASE`, `VAMP` | V | Top and base levels (most common level of a pulse, the extremes of a sine) and their difference |
| `FREQ`, `PERIOD` | Hz, s | From the average distance of all edges in the capture |
| `PWIDTH`, `NWIDTH` | s | High and low time at the 50 % level |
| `PDUTY`, `NDUTY` | % | Duty cycle |
| `RISE`, `FALL` | s | 10 % to 90 % of the amplitude, averaged over the edges, interpolated between samples |
| `OVERSHOOT`, `UNDERSHOOT` | % | Peak above the top and below the base, in percent of the amplitude |
| `PHASE` | deg | CH1 to CH2, -180 to 180, positive when CH2 lags |
| `DELAY` | s | CH1 rising edge to the nearest CH2 rising edge, averaged |

| Command | Description |
|---|---|
| `MEASure:<name>? [CHAN<n>]` | One value of channel 1 (default) or 2. `PHASE` and `DELAY` ignore the channel |
| `MEASure:ALL? [CHAN<n>]` | Every measurement, lines `<name>,<value>,<unit>` |
| `MEASure:SLOT<n> CHAN<c>,<name>` | Set F-key slot 1 to 6. The slots take the first twelve names (`VMAX` to `NDUTY`) |
| `MEASure:EXTRa` | `OFF`, `CHAN1`, `CHAN2`: the extra measurement panel on the scope (rise, fall, overshoot, undershoot, Vrms AC, Vrms, phase, delay) |
| `MEASure:STATistics` | `ON`, `OFF`: statistics over captures, also shown on the scope |
| `MEASure:STATistics:COUNt` | Window N: `10`, `20`, `50`, `100`, `200`, `500`, `1000` captures |
| `MEASure:STATistics:RESet` | Start the statistics again |
| `MEASure:STATistics:DATA?` | One line per tracked value: `SLOT<n>` or `EXTRA`, then `<channel>,<name>,<count>,<last>,<mean>,<min>,<max>,<std dev>` |

The statistics follow the six slots. While the extra panel is on, its eight values are tracked
too. They cover the last N captures. The standard deviation is the population standard
deviation of that window. Changing a slot's measurement starts its statistics again.

## Serial decoding

| Command | Values |
|---|---|
| `DECode:MODE` | `OFF`, `UART`, `I2C` |
| `DECode:UART:BAUD` | `AUTO`, `300`, `600`, `1200`, `2400`, `4800`, `9600`, `14400`, `19200`, `38400`, `57600`, `115200` |
| `DECode:UART:FORMat` | `8N1`, `8E1`, `8O1` |
| `DECode:UART:IDLE` | `HIGH` (TTL UART), `LOW` (inverted line, for example RS-232 measured before the level shifter) |
| `DECode:UART:SOURce` | `CHAN1`, `CHAN2` |
| `DECode:LIST` | `ON`, `OFF`: event list on the scope |
| `DECode:DATA?` | `#DECODE <mode> <status> <count> <baud>`, then one line per event |

I2C always uses CH1 for SDA and CH2 for SCL, so both channels must be on.

The `DECode:DATA?` event lines are `<type>,<start s>,<end s>,<value>,<flags>`:
- type: `BYTE` (UART), `START`, `STOP`, `ADDRESS` (7 bit address), `DATA`
- start and end: seconds relative to the trigger point
- flags:
  - `P` parity error
  - `F` framing error
  - `N` not acknowledged
  - `R` read address
  - `X` byte cut off by a start or stop

Status:
- 0: decoded
- -1: no signal
- -2: too few samples per bit (use a faster time base)
- -3: no baud rate found

The decoder needs at least 4 samples per UART bit and 2 samples per I2C clock phase. Choose
the time base so that the bytes of interest are in the capture. A capture holds about 3000
samples, so it holds about 3000 / (10 x samples per bit) UART bytes.

## Data

| Command | Reply |
|---|---|
| `WAVeform:DATA? CHAN<n>` | Same as `WAVE? <n>`: `#WAVE <ch> <count> <sample rate> <trigger index>`, then the samples in mV, ten per line |
| `WAVeform:RAW? CHAN<n>` | Same as `WAVEB? <n>`: a header with the scaling, then the raw 8 bit samples |
| `DISPlay:DATA? <row> <rows>` | Same as `SCREEN? <row> <rows>`: up to 16 screen rows as RGB565 |

These replies end with their own `#END`; send them on a line of their own.

## Original commands

The first X Scope commands still work as before (upper case, exact spelling):

| Command | Reply |
|---|---|
| `IDN?` | Name and version |
| `STATUS?` | Run state, time base, sample rate, channel and trigger settings |
| `KEYS?` / `KEY <name>` | List of key names / press a front panel key or turn a knob one step |
| `WAVEB? <1\|2>` | Header line with the scaling, then the raw 8 bit samples |
| `WAVE? <1\|2>` | The samples in millivolts as text |
| `SCREEN? <row> <rows>` | Up to 16 rows of the screen as RGB565 |

## What only the scope can show

The command layer and the analysis are tested on a PC: the parser, every setting, random input,
and measurements and decoders on synthetic waveforms. Timing and level accuracy against real
signals depends on the scope's own front end and has to be checked on the scope itself.

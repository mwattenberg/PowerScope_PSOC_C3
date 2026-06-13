# PowerScope PSoC C3

Demo firmware for the Infineon PSoC Control C3 (PSC3M5) that samples 8 ADC channels at high speed and streams the data to a host via UART or SPI, formatted for the PowerScope protocol.

## What this does

- Triggers the HPPASS ADC at a configurable sample rate (default 150 kHz) via a TCPWM timer interrupt
- Packs each set of ADC results into a PowerScope frame
- Transmits frames continuously over UART or SPI using DMA

## PowerScope frame format

Each frame is 18 bytes:

| Bytes | Content |
|-------|---------|
| 0–1   | Start sequence: `0xAA 0xAA` |
| 2–3   | Channel 0 (int16, little-endian) |
| 4–5   | Channel 1 (int16, little-endian) |
| ...   | ... |
| 16–17 | Channel 7 (int16, little-endian) |

Frames are transmitted back-to-back with no gap. The host synchronises by scanning for the `0xAA 0xAA` pattern.

## Interface

The active interface is selected at compile time by which SCB peripheral is passed to `PowerScope_init()`. The caller initialises and enables the SCB (UART or SPI master), then passes its hardware pointer:

```c
PowerScope_init(SPI_HW);   // or UART_HW
```

PowerScope reads `SCB_CTRL_MODE` at runtime — no `#define` needed.

## Hardware

Target: `KIT_PSC3M5_CC1` (PSoC Control C3M5 Digital Power Control Card)

| Signal | Pin |
|--------|-----|
| UART TX | P9[3] |
| SPI MOSI | configured in device configurator |
| SPI SCLK | configured in device configurator |
| SPI CS | configured in device configurator |

## Building

Requires [ModusToolbox](https://www.infineon.com/modustoolbox) v3.3 or later.

```
make build
make program
```

## License

MIT — see [LICENSE](LICENSE). The Infineon ModusToolbox framework and PDL drivers this project builds on are subject to their own license terms.

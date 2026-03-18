# R7250Adj

A Windows CLI utility for tuning power and thermal limits on the AMD Ryzen 7 250 (Phoenix/Hawk Point) using the PawnIO kernel driver

> [!WARNING]  
> **USE AT YOUR OWN RISK. AUTHOR IS NOT RESPONSIBLE FOR ANY DAMAGE YOU MAY CAUSE TO YOUR HARDWARE BY USING THIS TOOL**


#### :bangbang: NOTE FOR USERS
- **Claude Sonnet assisted development. Author is not a C++ or embedded systems developer. This is personal research tooling. Use at your own risk.**
- **Runs as Administrator with direct SMU hardware access. Understand what each parameter does before writing values.**
- **Exclusively supports the AMD Ryzen 7 250. It will refuse to run on any other CPU.**

## Prerequisites

- **PawnIO driver** installed and running — download from [https://pawnio.eu](https://pawnio.eu)
- **Administrator privileges** — required to communicate with the PawnIO device

Verify PawnIO is running before use:

```
sc query PawnIO
```

## Installation

1. Extract the release zip into a directory.
2. Open an elevated (Administrator) command prompt in the directory and run the app as per usage below.

## Usage

```
R7250Adj.exe --help
R7250Adj.exe --info
R7250Adj.exe --stapm-limit <milliwatts>
R7250Adj.exe --fast-limit  <milliwatts>
R7250Adj.exe --slow-limit  <milliwatts>
R7250Adj.exe --tctl-temp   <celsius>
```

### Examples

Print usage:

```
R7250Adj.exe --help
```

Read current values:

```
R7250Adj.exe --info
```

```
R7250Adj v1.0 | AMD Ryzen 7 250 (Phoenix/Hawk Point)
PM Table Version : 0x4C0009

Parameter           Current Limit    Safe Range
---------           -------------    ----------
STAPM Limit         15.000 W         10 - 53 W
PPT Fast Limit      25.000 W         10 - 53 W
PPT Slow Limit      15.000 W         10 - 43 W
Tctl Temp Limit     95.000 C         60 - 100 C
```

Set STAPM limit to 20 W:

```
R7250Adj.exe --stapm-limit 20000
OK: stapm-limit set to 20000 mW (20.000 W)
```

Set thermal limit to 90 C:

```
R7250Adj.exe --tctl-temp 90
OK: tctl-temp set to 90 C
```

## Safe Ranges

All values are validated before any hardware contact. Out-of-range inputs are rejected at exit code 1 without touching the hardware.

Maximum safe range allowed here is based on AMD's allowed max for fast-limit and Lenovo LOQ 15AHP10 firmware's max allowed limit in performance mode.

Your own laptop manufacturer may not support the same maximum values. You can find safe values for your own machine and if they are below max allowed here, the tool will work for you.

| Parameter | Unit | Minimum | Maximum |
|---|---|---|---|
| stapm-limit | mW | 10000 | 53000 |
| fast-limit | mW | 10000 | 53000 |
| slow-limit | mW | 10000 | 43000 |
| tctl-temp | C | 60 | 100 |

## CPU Identification

R7250Adj enforces two independent checks on startup to ensure it only runs on the exact target hardware:

1. **CPUID brand string** (pre-driver, zero hardware risk) — reads the CPU marketing name directly from the processor and checks for `"Ryzen 7 250"`. Rejects any other CPU before the PawnIO driver is accessed.
2. **PawnIO codename** (post-driver) — confirms the silicon family is Phoenix (`23`) or Hawk Point (`30`). Both share identical SMU mailbox addresses and command IDs.

## Exit Codes

| Code | Meaning |
|---|---|
| 0 | Success |
| 1 | Bad input — value out of range or unparseable |
| 2 | Hardware or driver error |

## Building

`build.sh` auto-detects the build environment — run it directly without any arguments.

On MSYS2 MinGW64 (Windows):

```bash
chmod +x build.sh
./build.sh
```

On WSL or Linux, install the cross-compiler first:

```bash
sudo apt install mingw-w64
chmod +x build.sh
./build.sh
```

Both produce `R7250Adj.exe` for Windows x64.

## Troubleshooting

**"Detected CPU does not match"**
- R7250Adj exclusively supports the AMD Ryzen 7 250. The CPUID brand string check fires before any driver interaction.

**"Unsupported CPU codename"**
- The PawnIO module identified a CPU silicon family other than Phoenix or Hawk Point. Not supported.

**"PawnIO driver is not installed"**
- Download and install PawnIO from [https://pawnio.eu](https://pawnio.eu)

**"PawnIO driver is not running"**
- Start the service: `sc start PawnIO`
- Confirm you are running the command prompt as Administrator

**"Cannot open PawnIO device"**
- PawnIO is running but the device handle could not be opened — confirm you are running as Administrator

**"Cannot find RyzenSMU.bin"**
- Ensure you extracted the full release zip — `RyzenSMU.bin` must be in the same directory as `R7250Adj.exe`

**"SMU rejected command"**
- The AMD firmware rejected the value via its own internal safety layer. Try a value further within the safe range.

## Credits

- [PawnIO](https://pawnio.eu) by namazso — GPL-2.0
- [PawnIO.Modules](https://github.com/namazso/PawnIO.Modules) by namazso — LGPL-2.1
- [RyzenAdj](https://github.com/FlyGoat/RyzenAdj) by FlyGoat — LGPL-3.0 (PM table offsets and SMU command IDs, verified on hardware)
- [UXTU](https://github.com/JamesCJ60/Universal-x86-Tuning-Utility) by JamesCJ60 — GPL-3.0 (PawnIO architecture and MP1 mailbox protocol reference)
- [ECReader](https://github.com/kulvind3r/ECReader) by kulvind3r — AGPL-3.0 (PawnIO interaction pattern reference)

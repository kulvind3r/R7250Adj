# HawkTune

A Windows CLI utility for tuning power and thermal limits on the AMD Ryzen 7 250 (Hawk Point) using the PawnIO kernel driver — no dynamic driver installation, no Windows Event Log noise.

## Prerequisites

- **PawnIO driver** installed and running — download from [https://pawnio.eu](https://pawnio.eu)
- **Administrator privileges** — required to communicate with the PawnIO device
- **RyzenSMU.bin** placed in the same directory as `HawkTune.exe` — download from [PawnIO.Modules releases](https://github.com/namazso/PawnIO.Modules/releases)

Verify PawnIO is running before use:

```
sc query PawnIO
```

## Installation

1. Place `HawkTune.exe` and `RyzenSMU.bin` in the same directory.
2. Run from an elevated (Administrator) command prompt.

## Usage

```
HawkTune.exe --help
HawkTune.exe --info
HawkTune.exe --stapm-limit <milliwatts>
HawkTune.exe --fast-limit  <milliwatts>
HawkTune.exe --slow-limit  <milliwatts>
HawkTune.exe --tctl-temp   <celsius>
```

### Examples

Print usage:

```
HawkTune.exe --help
```

Read current values:

```
HawkTune.exe --info
```

```
HawkTune v1.0 | AMD Ryzen 7 250 (Hawk Point)
PM Table Version : 0x4C0009

Parameter          Current Limit    Safe Range
---------          -------------    ----------
STAPM Limit        15.000 W         5 - 30 W
PPT Fast Limit     25.000 W         5 - 54 W
PPT Slow Limit     15.000 W         5 - 30 W
Tctl Temp Limit    95.000 C         60 - 100 C
```

Set STAPM limit to 20 W:

```
HawkTune.exe --stapm-limit 20000
OK: stapm-limit set to 20000 mW (20.000 W)
```

Set thermal limit to 90 C:

```
HawkTune.exe --tctl-temp 90
OK: tctl-temp set to 90 C
```

## Safe Ranges

All values are validated against AMD official specifications for the Ryzen 7 250 before any hardware contact. Out-of-range inputs are rejected at exit code 1 without touching the hardware.

| Parameter | Unit | Minimum | Maximum | Source |
|---|---|---|---|---|
| stapm-limit | mW | 10000 | 30000 | AMD cTDP: 15-30 W. 10 W minimum enforced for system responsiveness. |
| fast-limit | mW | 10000 | 54000 | AMD PPT Fast ceiling. 10 W minimum enforced for system responsiveness. |
| slow-limit | mW | 10000 | 30000 | AMD cTDP: 15-30 W. 10 W minimum enforced for system responsiveness. |
| tctl-temp | C | 60 | 100 | AMD tJMax: 100 C |

Source: [TechPowerUp — AMD Ryzen 7 250](https://www.techpowerup.com/cpu-specs/ryzen-7-250.c4011)

## Exit Codes

| Code | Meaning |
|---|---|
| 0 | Success |
| 1 | Bad input — value out of range or unparseable |
| 2 | Hardware or driver error |

## Building

Requires MinGW-w64 cross-compiler. On WSL or Linux:

```bash
sudo apt install mingw-w64
chmod +x build.sh
./build.sh
```

This produces `HawkTune.exe` which runs on Windows x64.

## Troubleshooting

**"Cannot open PawnIO device"**
- Confirm PawnIO is installed: `sc query PawnIO`
- Start the service if stopped: `sc start PawnIO`
- Confirm you are running the command prompt as Administrator

**"Cannot find RyzenSMU.bin"**
- Download `RyzenSMU.bin` from [PawnIO.Modules releases](https://github.com/namazso/PawnIO.Modules/releases)
- Place it in the same directory as `HawkTune.exe`

**"Unsupported CPU"**
- HawkTune only supports Hawk Point (Ryzen 7 250). It will refuse to run on any other CPU.

**"SMU rejected command"**
- The AMD firmware rejected the value. This is the hardware's own safety layer enforcing its internal limits. Try a value further within the safe range.

## Credits

- [PawnIO](https://pawnio.eu) by namazso — GPL-2.0
- [RyzenSMU.p](https://github.com/namazso/PawnIO.Modules) by Erruar — LGPL-2.1
- [ECReader](https://github.com/kulvind3r/ECReader) by kulvind3r — AGPL-3.0
- [RyzenAdj](https://github.com/FlyGoat/RyzenAdj) by FlyGoat — LGPL-2.1 (reference for PM table offsets and SMU command IDs)
- [UXTU](https://github.com/JamesCJ60/Universal-x86-Tuning-Utility) by JamesCJ60 — GPL-3.0 (reference for PawnIO architecture and MP1 mailbox protocol)

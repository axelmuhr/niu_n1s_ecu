# NIU N1S – ECU / Telematics Control Unit – Firmware Analysis

Analysis of the Ghidra disassembly `NIU.asm` (Intel HEX, flash `0x08000000`–`0x0800FFFF`, 64 KB).

## 1. Hardware Platform

| Property | Finding | Evidence |
|---|---|---|
| MCU | STM32F103, **High-Density** (VC/RC/ZC class) | DMA2 present (`0x40020404/408`), 4 timers + 3 USARTs active |
| Reset SP | `0x200015E8` | Vector table `0x08000000` |
| Firmware framework | ST **Standard Peripheral Library** (SPL) | generic TIM/USART/ADC/GPIO init helpers (e.g. `FUN_0800546c` checks TIM1/TIM8 vs. TIM2–5) |
| Compiler runtime | Keil/ARM (`__main`, `strstr`, `atoi`, `memset`, `memcpy`) | named library calls |

### Vector Table / Interrupt Handlers
| Vector | Address | Meaning |
|---|---|---|
| Reset | `0x08000100` → `FUN_08000e18` | SystemInit, then `main` |
| NMI/HardFault/… | `0x080008dc` / `0x08000669` … | fault handlers |
| SysTick | `0x08000df8` | 1 ms time base |
| IRQ14 = DMA1_Ch4 | `FUN_0800020c` | USART1 RX DMA (vehicle bus) |
| IRQ29 = TIM3 | `FUN_08000e68` | timer ISR |
| IRQ37 = USART1 | `LAB_08000edc` | USART1 idle line (frame length) |

## 2. Peripheral Assignment (from register bases)

| Peripheral | Base | Role |
|---|---|---|
| **USART2** `0x40004400` | DMA1 Ch6/7 | **GSM/GPRS modem Neoway M590E** (AT commands) |
| **USART3** `0x40004800` | DMA1 Ch3, 1024-B buffer | **GPS u-blox MAX-7Q** (NMEA reception) |
| **USART1** `0x40013800` (PA9/PA10), **9600 8N1**, DMA1 Ch4 + idle IRQ, 128-B buffer, DE/RE via GPIOB | **RS-485 vehicle bus** (dashboard/BMS/motor controller), frames with `0x68` start byte |
| **ADC1** `0x40012400` | DMA1 Ch1 → `0x20000174` | analog values (battery/sensors) |
| **SPI1** `0x40013000` | master | external SPI device (accelerometer/alarm or EEPROM) |
| TIM1/2/3/4 | `0x40012C00`/`0x40000000`/`…0400`/`…0800` | time bases, PWM, input capture |
| **RTC** `0x40002800` | 18 accesses | time + timezone (timestamps) |
| IWDG `0x40003000` | | watchdog |
| **FLASH** `0x40022000` | 32 accesses | on-chip flash as EEPROM emulation (settings/odometer) |
| GPIOA/B/C, AFIO, EXTI, RCC | | I/O, clock, external interrupts |

Central init functions: `FUN_080043e4` enables USART1/2/3 + TIM1/2/3/4 + ADC1;
`FUN_08004338` initializes GPIO + SPI1; `FUN_08002b40` starts ADC1+DMA.

## 3. Functional Blocks

### 3.1 GSM/GPRS – Neoway M590E (USART2)
AT commands (Neoway `$MY…` dialect), one small wrapper function per command:

- **Init/Info:** `ATE0` (echo off), `AT$MYGMR` (FW version), `AT+CGSN` (IMEI),
  `AT$MYCCID` (SIM CCID), `AT+CPIN?` → `READY`, `AT+CSQ` (signal), `AT+CREG?` (network),
  `AT+ENPWRSAVE=1` (power save).
- **PDP/APN:** `AT$MYNETCON=0,APN,<apn>` + `USERPWD`/`AUTH`, `AT$MYNETACT=0,1` / `AT$MYNETACT?`.
- **TCP socket:** `AT$MYNETSRV=` (set server), `AT$MYNETOPEN`/`…CLOSE`,
  `AT$MYNETWRITE=0,<len>` (send), `AT$MYNETREAD=…,2048` (receive),
  `AT$MYNETURC=1` + URC parsing `MYURCREAD`/`MYNETWRITE`/`MYNETREAD`
  (`FUN_080065dc` parses the unsolicited result codes).
- **Alternative HTTP engine:** `AT+HTTPSETUP`, `AT+HTTPPARA=url,`, `AT+HTTPACTION=0`,
  `AT+HTTPCLOSE`, `HTTPRECV`, `Content-Length`, `AT+POSI=1`.

**Automatic APN selection:** the SIM CCID is read and its prefix (`894700`, `898606`,
`893144`, `891039`, `899770`, `894301`, `894230`) is mapped to the matching APN
(telenor, 3g.net, niutech, Pulic4.m2minternet.com, smart, m2m.tag.com, CMNET).
Fixed credentials: `USERPWD "tagm2m,m2m572"`, `AUTH 1`.

### 3.2 GPS – u-blox MAX-7Q (USART3) — `FUN_08006708`
Reception via DMA1 Ch3 into a 1024-B buffer (`0x20001AFC`), evaluation via `strstr`:

| NMEA | Sub-parser | Content |
|---|---|---|
| `$GPRMC` | `FUN_08003b30` | time/date, position, speed, fix validity → sets bit `0x2` in status `0x20000140` |
| `$GPGGA` | `FUN_08003970` | altitude, fix quality, satellite count |
| `$GPGSA` | `FUN_080039d0` | fix type, DOP |
| `$GPGSV` | `FUN_08003a30` | satellites in view |

Parsed structure at `0x200015B8`; post-processing `FUN_0800787c`. A seconds counter
`0x2000014C` (limit `0xE10` = 3600) triggers `FUN_0800c050` (hourly task).

### 3.3 ADC / Battery & Sensors (ADC1 + DMA) — `FUN_08002b40`
ADC1 in scan/DMA mode, raw values → `0x20000174`. Provides battery voltage/current,
temperature, and possibly throttle/analog sensors. The core BMS data (SOC, cell voltages,
etc.) additionally arrives over the serial vehicle bus (USART1).

### 3.4 RS-485 Vehicle Bus / Dashboard (USART1) — `FUN_0800bf00`
USART1 on PA9/PA10, **9600 8N1** (init in `FUN_08003ee0`, baud arg `0x2580`), MAX485
transmit/receive direction via a GPIOB line (DE/RE). DMA1 Ch4 reception into a 128-B
buffer + idle-line IRQ (variable frame length). The motor controller/BMS **and the LCD
speedometer unit** hang on this bus; frames carry the start byte `0x68` (details in
section C). From them the ECU reads speed, battery level/current, temperature and
odometer – the same values the dashboard displays (see section G).

### 3.5 Server Backend & Remote Control
- **Endpoint:** `fk-ecu.niu.com` (NIU cloud), TCP via the M590 socket commands.
- **Packet build/registration** (`FUN_08009xxx`/`FUN_0800a…`): embedded identifiers
  `TRA01C19` (protocol/FW tag), frame number **`N1SB4CAR242730LZ`** (`0x0800F800`),
  default ID `111111111`. Base36/hex table `0123456789…XYZ` for field encoding.
- **Remote commands (server → scooter)** — dispatcher `FUN_08008a64`, `strstr` per token:

| Command | Effect | State bit |
|---|---|---|
| `unlock` / `lock` | release/set immobilizer | bit0 in `0x20002030` |
| `setmileage <n>` | set odometer (+ flash save `FUN_0800b8b4`) | `0x20000134/138` |
| `clear` | reset odometer (−1000) | `0x20000134/138` |
| `settimezone <n>` | timezone (+ save `FUN_0800b934`) | `0x20002032` |
| `metric` / `imperial` | unit km/mi | bit0 in `0x20002011` |
| `opengps` / `closegps` | GPS on/off | bit `0x100` in `0x20000140`, bit `0x8` in `0x20002030` |
| `updata` | firmware/data update (`FUN_08007258`) | result → `0x20001A40` |

Before evaluation the dispatcher checks a 16-byte header (device/signature comparison)
and filters out ` ERROR` / `null,null`.

## 4. Key RAM State Variables

| Address | Meaning |
|---|---|
| `0x20000140` | global status word (bit0x2 = GPS fix, bit0x100 = GPS off) |
| `0x20002030` | state byte (bit0 = lock, bit0x8 = GPS closed) |
| `0x20002011` | unit flag (bit0 = imperial) |
| `0x20002032` | timezone offset |
| `0x20000134/138` | odometer / trip |
| `0x20000014` | "settings changed – save" flag |
| `0x20000174` | ADC DMA buffer |
| `0x200015B8` | parsed GPS data |
| `0x20001AFC` | GPS NMEA receive buffer (1024 B) |

## 5. Overall Function (Summary)
The module is the **telematics/control unit** of the NIU N1S. It
1. reads the vehicle/BMS data (speed, battery, warnings) over USART1 and feeds the
   LCD speedometer unit,
2. determines the GPS position over USART3 (u-blox),
3. establishes a GPRS/TCP connection to `fk-ecu.niu.com` over USART2 (Neoway M590E),
   reports position/status and receives remote commands,
4. executes remote commands (lock/unlock, odometer, units, timezone, GPS on/off,
   OTA update), and
5. persists settings in the flash EEPROM emulation, and supervises itself via watchdog.

---

# Deep Dive of Selected Blocks

## A. Main Loop `FUN_0800a2d4` (application thread)
```
VTOR ← 0x08000000              (FUN_080044fc, relocate vector table)
interrupts enabled (faultmask/primask = 0)
FUN_08007650   HW init: GPIO, SPI1, ADC, motion EXTI, module power
FUN_0800a1f8   app init: load VIN/model (FUN_08009fc8)
loop:                          ; infinite loop
  FUN_08008620   ensure GSM/TCP connection to fk-ecu.niu.com:8888
  FUN_0800baf8   assemble/send report   (buffer 0x200000AD)
  FUN_080082fc / 08008380 / 0800b948 / 080085c0 / 0800b9d0
                 gather data fields (BMS, GPS, status)
  FUN_08007a78   special/OTA sequence handler
  FUN_080086e8   timer/housekeeping
  FUN_08003eac   reload watchdog (IWDG)
  → loop
```

## B. GPS `$GPRMC` Parser `FUN_08003b30`
Field access via `FUN_08003d48(n, str)` = "return the offset of the n-th comma-separated
field" (reused 20×, also for the server protocol). Target structure `g_gps_data`
(`0x200015B8`):

| Offset | Content | Source |
|---|---|---|
| `+0x00` | latitude (double, decimal degrees) | field 3 `ddmm.mmmm` → `deg + min/60` (×`1/60`), sign − for `S` |
| `+0x08` | longitude (double, decimal degrees) | field 5, sign − for `W` |
| `+0x14` | speed (knots) | field 7 |
| `+0x24/28/2c` | year / month / day | field 9 `ddmmyy` (ASCII×10+digit) |
| `+0x30/34/38` | hour / minute / second | field 1 `hhmmss` |
| `+0x40/41` | N/S resp. E/W indicator | field 4/6 |
| `+0x7f` | fix-valid flag (`DAT_20001637`) | field 2 = `A` valid / `V` invalid |

Constants: `0x3FFD9999999A ≈ 1/60`, `0x42700000 = 60.0` (NMEA degree conversion).
Analogous sub-parsers: `$GPGGA→FUN_08003970`, `$GPGSA→FUN_080039d0`, `$GPGSV→FUN_08003a30`.

## C. Two Separate Protocols — Assignment Correction

> **Important:** the binary `0x68` frame protocol runs on **USART1 = RS-485 vehicle bus**
> (`FUN_0800654c` reads `0x40013800`), **not** on the GSM/server side. The server traffic
> (fk-ecu.niu.com) is **text-based** (commands `unlock`/`lock`/…, report strings).

### C1. RS-485 Bus Protocol (USART1, 9600) — reception `FUN_08002e34` (from `FUN_0800654c`)
Byte-wise state machine, frame in buffer `g_frame_rx_buf` (`0x200001E8`),
state `0x20000090`, index `0x20000268`:

```
[0]   = 0x68            start delimiter
[1]   = type/command    ┐  header check: [1] + [2] == 0xFF
[2]   = ~[1]            ┘
[3..4]= ...
[5]   = N (payload length)   → total length = N + 6
[6..] = payload
[len-1]= checksum = (Σ bytes[0..len-2]) & 0xFF
```
A complete frame → **`FUN_080072dc`** validates (length, `[1]+[2]==0xFF`, checksum)
and branches on type `[1]`. `FUN_08008a44` is the additional **rolling XOR checksum**
(light obfuscation) used when building/checking:

| Type | Handler | Meaning (bus node/message) |
|---|---|---|
| `0x10` | `FUN_0800a10c` | config/control |
| `0x20` | `FUN_080089d4` | vehicle status (sub-dispatch on `[4]&0x1F`: BMS/controller) |
| `0x31` | `FUN_080075e0` | controller/display record |
| `0x40` | `FUN_08007ddc` | query/ACK |
| `0x70` | `FUN_0800a12c` | diagnostics/update |

From types `0x20`/`0x31` the ECU extracts the vehicle data (speed, battery %, current,
odometer) → the same quantities the dashboard shows (section G).

### C2. Server Protocol (GSM/TCP, fk-ecu.niu.com:8888) — text-based
- **Reception:** GSM socket read (`gsm_socket_read`/`FUN_08008f90`) → buffer → in the main
  loop `FUN_080082fc` → **`FUN_08008a64`** `strstr` dispatch of the text remote commands
  (unlock/lock/setmileage/…).
- **Sending:** `FUN_0800baf8` builds the report in buffer `0x200000AD` and pushes it out via
  `AT$MYNETWRITE=0,<len>` (USART2/M590E). Fields are encoded with the table
  `0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ` (`0x0800A620`); device identity from
  `g_vin_config` (VIN `N1SB4CAR242730LZ`), protocol tag `TRA01C19`.

## D. OTA Command Parser `FUN_08007258` (target of the `updata` text command)
Splits the `updata` payload via `FUN_08003d48` and stores the update parameters in the
structure at `0x20001A40`:

| Field | Target | Meaning |
|---|---|---|
| 2 | `0x20001AD2` (memcpy) | host/server |
| 3 | `0x20001AC2` (memcpy) | file/path |
| 4 | `0x20001AE4` (atoi) | size/length |
| 5 | `0x20001AC1` (atoi) | type/flag |
| 6 | `0x20001A41` (strcpy) | version/checksum |

Triggered via flag `0x20001A40`; the actual OTA sequence (battery/GPS check, module power)
runs in `FUN_08007a78`.

## E. Flash EEPROM Emulation `FUN_0800376c`
Stores the RAM settings block `0x20002000` (lock, unit, timezone, odometer, VIN config)
to the flash page **`0x0800FF00`**: disable IRQs → erase page (`FUN_0800364c`) → program
half-words (`FUN_080036d8`) → `FLASH_Lock` (`FUN_080036b0`). SPL flash driver:
`FUN_08003688` status, `FUN_0800374c` wait, `FUN_08003640` clear-flag.

## F. Model/VIN Detection `FUN_08009fc8`
Copies the 36-byte VIN from `0x0800F800` to `g_vin_config`. Based on the 1st VIN character
(`U`/`M`/`N`) and following characters, model-specific **speed calibration factors**
(wheel/gear factor as float, e.g. `1.35`, `1.28`) are set at `0x20000168` – the basis of
the speed calculation for the display.

## G. Speedometer / Dashboard (external assembly)
Sources: `dashboard.txt` (STM8CubeMX pinout), `ht1621.h`, `main.c` (alternative firmware).

The dashboard is a **standalone controller** – **STM8S105S4T6**, not part of the STM32
control unit. It is connected to the ECU's USART1 via the **RS-485 bus** (MAX485
transceiver) and displays the values delivered by the ECU/controller.

**Connections (STM8):**
| Pin | Function | Role |
|---|---|---|
| PD5/PD6 | UART2 TX/RX | RS-485 bus to the ECU |
| PC1 | GPIO out | MAX485 DE/RE (transmit/receive direction) |
| PC2 / PC3 / PC5 | GPIO out | HT1621 `nCS` / `nWR` / `DATA` (3-wire LCD) |
| PD2 | GPIO out | LCD backlight |
| PB4 | ADC IN4 | temperature sensor |
| PB0 / PB2 | GPIO out | turn signal left / right |
| PB1 | GPIO out | battery LED |
| PE5 / PE6 | GPIO out | park (red) / high beam (blue) |
| PA4 / PA5 / PA6 | GPIO in | buttons/switches |

**LCD controller:** Holtek **HT1621** (RAM-mapped segment LCD, 4 commons, 1/3 bias,
on-chip RC). Commands per `ht1621.h` (`Sys_en 0x02`, `LCD_on 0x06`, `ComMode 0x52`,
`RCosc 0x30`; prefixes `0x80`=cmd, `0xa0`=data). LCD RAM = 32 nibbles.

**Segment map (nibble addresses, from `main.c`):**
| Addr | Field | | Addr | Field |
|---|---|---|---|---|
| 1/3 | speedo digit 2/1 | | 16/18 | charge % digit 2/1 |
| 4 | drive mode | | 19 | special signs (bars/`REVERSE 0x2`/`ECO 0x4`) |
| 6/8/10/12/14 | odometer digit 5…1 | | 21/23 | temperature digit 2/1 |
| 24/25/26 | charge bars chrg_3…1 | | 27–30 | ampere bars (each bit = 3 bars) |

**Special-sign bits:** `KMH`(1) `PERCENT`(3) `MODE_SIGN`(4) `KM`(6) `AMPERE`(16)
`BARS`(19) `CELSIUS`(21) `OVERHEAT`/`PLUG`(26). **7-segment font** (digits `0xFA,0x60,
0xD6,…`) and letters `A/E/H/L/N/U` tabulated in `main.c`.

**Correspondence to the ECU:** the values decoded in section H (bus types `0x20`/`0x31`)
fill exactly these LCD fields: speed → speedo, odometer → odometer, battery % → charge,
current → ampere bars.

> **Note on the alternative dashboard firmware (`main.c`):** it initializes UART2 at
> **38400 8N1**. The OEM ECU, however, drives the bus at **9600 8N1** (USART1 init
> `FUN_08003ee0`, baud arg `0x2580`). For interoperability with the original bus/ECU the
> dashboard firmware would need to be set to **9600**. (`main.c` is an experimental state
> anyway: the protocol parser is not yet implemented, `uart_read_n_byte` reads only fixed
> lengths, and the HT1621 writes are test patterns.)

## H. Bus Frame → Vehicle Data: byte-level mapping
Decoded from the type handlers `FUN_08008734` (type 0x31) and `FUN_080089d4`/`LAB_080088b8`
(type 0x20). **All payload bytes are obfuscated by `+0x33` on the wire**; the parser first
subtracts `−0x33` from every payload byte (loop at `0x08008734`, resp. inline at
`0x080088b8`). A sub-state selector `0x20000314` chooses the record layout (request/response
protocol). u16 fields are **big-endian** (`[hi]<<8 | [lo]`). Offsets = byte index in the
frame (payload starts at `[6]`).

### Type 0x31 – controller/display record (`FUN_08008734`, when `[2]=0x2d,[3]=0x0f`)
| Frame offset | Target RAM | Type | Meaning |
|---|---|---|---|
| `[6..7]` | `200002C2` | u16 | raw value (voltage?) |
| `[8..9]` | `200002C4` | u16 | raw value (scaled ×100 in the report) |
| `[10..11]` | `200002C6` | u16 | raw value |
| **`[12]`** | **`200002C8`** | u8 | **battery charge % (clamped to 0…100)** → display "charge" |
| `[13..14]` | `200002CA` | u16 | raw value |
| `[15..20]` | `200002CC…D1` | u8×6 | status/flags |

`[12]<3` additionally switches a GPIOB lamp. A filtered 16-bit value (10-sample smoothing
`FUN_08007164`) → `2000013E`.

### Type 0x20, sub-2 – BMS/drive record (`LAB_080088b8`, when `0x20000314=5`)
| Frame offset | Target RAM | Type | Meaning |
|---|---|---|---|
| `[6..9]` | `20000306…309` | u8×4 | status/counters |
| `[10..11]` | `2000030A` | u16 | raw value |
| `[12..13]` | `2000030C` | **s16** | signed value → magnitude×100 in the report (charge/discharge current %) |
| **`[14..15]`** | **`2000030E`** | **s16** | **speed** (signed; sign = direction) |
| `[16..17]` | `20000310` | u16 | raw value |
| `[6+i]` (state=3) | `200002F5+i` | u8[] | variable array (cell voltages?) |

### Derived quantities (confirmed via report encoder `FUN_0800aafc`)
- **Speed (display):** `2000030E × g_speed_calib × 0.23…` → `FUN_08007804` continuously
  integrates the distance from it → **odometer `20000134` / trip `20000138`** (in m).
  In the server report: speed → `200002D7`, trip/1000 (km) → `200002D8`,
  battery % → `200002DC/DD`.
- **Temperature (display):** does **not** come from the bus frame, but is measured
  **locally** on the dashboard via ADC channel IN4/PB4 (`temperature` in `dashboard.txt`).

> Confidence of the assignment: **high** for the `+0x33` descrambling, battery % (`[12]`,
> clamped to 100) and speed (`[14..15]`, fed directly into the odometer integration).
> **Medium** for the remaining u16 raw values (exact voltage/current) – the offsets are
> fixed, but the physical unit is not yet definitively assigned.

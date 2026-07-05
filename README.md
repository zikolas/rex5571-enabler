# ES1688GO — a DOS enabler for ES1688 PCMCIA sound cards

A single-command DOS point enabler that brings ES1688–based PC Cards to life.
SB direct-dac audio, ESFM synthesis, MPU-401 UART port, and gameport — with no
Card Services or Socket Services needed. It programs the PCMCIA host controller and the ES1688 directly.

It started as a DOS enabler for one specific card — the Ratoc REX-5571, whose DOS
driver was lost to time — and grew into a general ES1688 enabler covering several
cards and likely more.

## Cards and features

| Card | SB Direct-DAC | ESFM @ `0x220` | AdLib @ `0x388` | MPU-401 | Gameport |
|---|:---:|:---:|:---:|:---:|:---:|
| Ratoc **REX-5571**<br>**REX-5572** | ✅ | ✅ | ✅ | ✅ | ✅ |
| Panasonic **KXL-C101**<br>**KXL-D20**<br>**KXL-D745** | ✅ | ✅ | ✅ | ❌ | ❌ |
| Eiger Labs **EPX-AA2000**<br>Fujitsu **16-bit Stereo** | ✅ | ✅ | ✅ | ❌ | ❌ |
| IBM **PCMCIA CD-ROM**<br>**1969-011**<br>**1969-111**<br>**CD-400S** | ✅ | ✅ | ✅ | ❌ | ❌ |

**Legend:** ✅ works · ❌ not possible · ❓ not yet confirmed.

## The tool behind it

This project exists because of one tool: **[COMrade](https://github.com/yyzkevin/COMrade)**
by **yyzkevin**. It's a small resident program that runs on vintage machines
and bridges them to a modern assistant over a null-modem cable — exposing the raw
hardware: read or write any I/O port, peek and poke memory, run DOS commands, and
push files onto the machine and build them there.

That turned a 30-year-old palmtop — an **IBM PC110** with an Intel 82365-class
PCMCIA controller — into something you could develop against like a live REPL. The
whole enabler was written, compiled **on the actual hardware** with Open Watcom,
run, and refined without anyone needing to sit in front of the machine.

The loop:

1. **Reason** from the card's own CIS and the public Intel 82365SL and AD1848 /
   CS4231 register models — what each register does.
2. **Poke it live** over COMrade — power the socket, map a PCIC window, write the
   config register, prod the codec — and **read the result back** the same instant.
3. **Build on the machine** — deploy the C source and compile it with Open Watcom
   right there on the palmtop.
4. **Run it, and listen.** The one thing a serial cable can't carry is sound — so
   the human at the other end simply says *"I can hear it!"*

## Usage

Run once; the configuration sticks in the controller until power-off or suspend.
ES1688GO only programs the hardware — you set the `BLASTER` variable yourself.

```
ES1688GO
SET BLASTER=A220 I5 D1 T4
```

With no `/S`, ES1688GO scans every socket, identifies the card from its CIS, and
configures the first known one it finds (tagged `(auto)` in its output). Match your
`SET BLASTER` values to the ports/IRQ it reports.

```
ES1688GO [/SB[=220]] [/FM[=388]] [/MPU[=330]] [/JOY] [/I=5] [/S=0..7] [/W=D000]
       [/FORCE] [/OFF]
  /SB[=hex]   SB base I/O port + ESFM (always enabled; default 220)
  /FM[=hex]   AdLib FM port (default 388)
  /MPU[=hex]  MPU-401 UART port (default 330; one of 300/310/320/330)
  /JOY        gameport / joystick at 201
  /I=dec      IRQ (default 5; MPU needs 5/7/9/10)
  /S=dec      socket number 0..7 (default: auto-detect the card)
  /W=hex      attribute-memory window segment used to write config (default D000)
  /FORCE      configure a socket without the CIS identity check (requires /S)
  /OFF        power the socket down and exit
```

With no `/FM` `/MPU` `/JOY`, the default add-ons are **FM + MPU** — and MPU is
dropped automatically on cards that can't use it. A card whose CIS is unreadable or
simply not in the built-in list can still be brought up with **`/FORCE`** (needs
`/S`); the DSP self-test then confirms an ES1688 is actually answering, so it won't
misconfigure an empty or non-ES1688 socket.

Per-card details (COR base/index, the amp-enable, no-MPU flagging) are handled from
a small built-in manifest — you don't need to know any of it to use a listed card.

## Building

Built with **Open Watcom 1.9** (16-bit, real mode). `ES1688GO.EXE` is included
prebuilt; to rebuild:

```
wcc -ms ES1688GO.C -fo=ES1688GO.obj
wlink system dos name ES1688GO.exe file ES1688GO.obj
```

### Clean-room note

This implementation derives **only** from (a) each card's own CIS, read off the
hardware, and (b) public specifications: the Intel 82365SL controller register set,
the PCMCIA standard, and the public ES1688 register model. No part of it comes from
disassembling any vendor driver.

## Credits

- **[COMrade](https://github.com/yyzkevin/COMrade)** by **yyzkevin** — the
  serial/MCP bridge that made remote, real-hardware development possible. This
  project simply would not exist without it.
- **[The PCMCIA Sound Card Spreadsheet](https://docs.google.com/spreadsheets/d/181yznQ-DEMQRVl0X3s09NH7WD87eM3ui16isHKjIz5c)**
  maintained by **Bondi** — A very useful source of PCMCIA Sound Card Information.

## License

Copyright (c) 2026 zikolas. MIT License — see [LICENSE](LICENSE).

## Disclaimer

A hobby project, shared in the spirit of retro tinkering. It pokes PCMCIA
controller and sound-chip registers directly on 30-year-old hardware:

- **Provided as-is, no guarantees** — it may or may not work with your card, your
  machine, or your particular phase of the moon.
- **No warranty and no responsibility for any damage** — to hardware, data,
  software, or sanity. You run it entirely at your own risk.
- It's only been exercised on the hardware named above; elsewhere, your mileage may
  vary.

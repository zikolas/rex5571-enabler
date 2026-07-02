# REXENA — a DOS enabler for ES1688 PCMCIA sound cards

A single-command DOS **point enabler** that brings ESS ES1688–based PC Cards
to life — Sound Blaster direct-dac audio, ESFM music, MIDI, and gameport — with
**no Card Services, no Socket Services, and no vendor driver**. It programs the
PCMCIA host controller and the ES1688 directly, then gets out of the way.

> **Why "REXENA"?** It started life as the *REX ENAbler* for one specific card,
> the Ratoc REX-5571, whose DOS driver was lost to time. It has since grown into a
> general ES1688 enabler covering several cards — but it keeps the name as a nod to
> where the journey began.

## What this unlocks

- **Ratoc REX-5571** — DOS sound on a card whose DOS driver was lost to time.
- **Ratoc REX-5572** — DOS MPU-401 MIDI, which it had no path to before (its twin,
  the 5571, already had it).
- **IBM PCMCIA Portable CD-ROM Drive** — DOS sound from its ES1688; the vendor only
  ever supported it under Windows.
- **Cards with a damaged CIS** — `/FORCE` configures a card whose CIS is corrupt or
  unreadable, self-testing the DSP to confirm an ES1688 is really present.
- **One tool, no drivers** — auto-detects the whole ES1688 family (and its rebadges)
  with no Card Services, Socket Services, or vendor software, and no resident
  footprint: it runs once and exits.

## Cards and what works

| Card | SB Direct-DAC | ESFM @ `0x220` | AdLib @ `0x388` | MPU-401 | Gameport |
|---|:---:|:---:|:---:|:---:|:---:|
| Ratoc **REX-5571** | ✅ | ✅ | ✅ | ✅ | ✅ |
| Ratoc **REX-5572** | ✅ | ✅ | ✅ | ✅ | ✅ |
| Panasonic/KME **KXL-C101**<sup>1</sup> | ✅ | ✅ | ✅ | ❌ | ❌ |
| Eiger Labs **EPX-AA2000**<sup>2</sup> | ✅ | ✅ | ✅ | ❌ | ❌ |
| IBM **PCMCIA CD-ROM** | ✅ | ✅ | ✅ | ❌ | ✅ |

<sup>1</sup> Also sold as the **Panasonic KXL-D20** and **KXL-D745**.
<sup>2</sup> The **Fujitsu 16-bit Stereo Sound Card** carries the same ID and is also supported.

**Legend:** ✅ works · ❌ not available on that card. All confirmed on real
hardware.

Every listed card does the ES1688 core — Sound Blaster Pro direct-DAC, ESFM at the
SB base, and AdLib at `0x388`. The differences are only at the edges:

- **MPU-401** is off for the Panasonic, Eiger and IBM box — the first two have no
  MIDI connector, and the IBM box's CD-ROM bridge doesn't decode enough address
  lines to expose the MPU port (it aliases onto the SB/IDE ports, so enabling it
  just adds noise). REXENA drops MPU automatically on those; the RATOC twins keep
  it. (See [MIDI](#midi-mpu-401) for how MIDI reaches a synth.)
- **Gameport** is present on the RATOC twins and the IBM box, but not on the
  Panasonic or Eiger.

## The story: built remotely, on real hardware, by ear

This project exists because of one tool: **[COMrade](https://github.com/yyzkevin/COMrade)**
by **yyzkevin**. It's a small resident program that runs on the vintage machine
and bridges it to a modern assistant over a plain serial cable — exposing the raw
hardware: read or write any I/O port, peek and poke memory, run DOS commands, and
push files onto the machine and build them there.

That turned a 30-year-old palmtop into something you could develop against like a
live REPL. The whole enabler was written, compiled **on the actual hardware**, run,
and refined without anyone sitting in front of the machine — an **IBM PC110**
palmtop and an **IBM ThinkPad 755C**, both with Intel 82365-class PCMCIA
controllers.

The feedback loop is the magic part:

1. **Reason** from the card's own CIS and the public ES1688 datasheet — what
   register does what.
2. **Poke it live** over COMrade — set a PCIC window, write a mixer register, reset
   the DSP — and **read the result back** the same instant.
3. **Build on the machine** — deploy the C source and compile it with Open Watcom
   right there on the palmtop.
4. **Run it, and listen.** The one thing a serial cable can't carry is sound — so
   the human at the other end simply says *"I can hear it!"* (or "still silent"),
   and the loop goes round again.

The IBM box's revival is the clearest example. The ES1688 answered every register,
its FM synth clocked perfectly — and it was **dead silent**. The datasheet noted
two general-purpose output pins "for power management." A single byte written to
the chip's GPO port over COMrade, and the box's amplifier thumped to life. A
capability that had been Windows-only for 30 years, brought back to DOS by reading
a PDF and flipping one bit — remotely, confirmed by ear.

Where it started: one elusive sound card, invisible to DOS. Where it got to: a
general enabler for a whole family of ES1688 PC Cards, including one whose sound had
never worked in DOS at all. **Thank you, yyzkevin** — none of it happens without
COMrade.

## What you get (and the DMA caveat)

A PCMCIA socket on an 82365 controller has **no DMA channel** — the controller
never bridges the card's DMA request to the system, and these cards declare none.
That splits Sound Blaster support cleanly:

**Works — no DMA needed:**

- **SB Pro DSP** detection and mixer access, so games *find* the card
- **Direct-DAC (PIO) digitized audio** — the CPU feeds each sample to the DSP
  directly. Real speech and sound effects play this way (*Another World*, *Magic
  Pockets*, FastDoom's **Sound Blaster Direct** output, and similar).
- **ESFM / AdLib FM** music, at the SB base and `0x388`
- **MPU-401 MIDI** (on cards that expose it)

**Doesn't work — needs DMA:**

- **DMA-based SB digitized playback** — common from the mid-'90s on. These games
  play FM/MIDI fine but are silent on digital audio. When a game offers a
  **"Sound Blaster Direct"**-style option, choose it over plain "Sound Blaster."

### MIDI (MPU-401)

The ES1688's MPU-401 is a MIDI *port*, not a synthesizer — there's no onboard
wavetable, so you connect an external module (MT-32, SC-55, a Roland CM-32L, etc.).
On the cards that expose it, the MIDI signal comes out on the **gameport
connector's MIDI pins** via a standard gameport-to-MIDI cable — which is how the
5572, with only a gameport and no dedicated MIDI jack, still drives a synth.

## Usage

Run it once; the configuration sticks in the controller until power-off or suspend
(it is **not** a TSR). REXENA only programs the hardware — you set `BLASTER`
yourself and launch your game:

```
REXENA
SET BLASTER=A220 I5 D1 T4
```

With no `/S`, REXENA scans every socket, identifies the card from its CIS, and
configures the first known one it finds (tagged `(auto)` in its output). Match your
`SET BLASTER` values to the ports/IRQ it reports.

```
REXENA [/SB[=220]] [/FM[=388]] [/MPU[=330]] [/JOY] [/I=5] [/S=0..7] [/W=D000]
       [/FORCE] [/OFF]
  /SB[=hex]   Sound Blaster base I/O port (always enabled; default 220)
  /FM[=hex]   dedicated AdLib/ESFM FM port (default 388)
  /MPU[=hex]  MPU-401 MIDI port (default 330; one of 300/310/320/330)
  /JOY        gameport / joystick at 201 (works alongside /FM and /MPU)
  /I=dec      IRQ (default 5; MPU requires 5/7/9/10)
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

## How it works (the short version)

A "point enabler" does by hand what Card + Socket Services would do automatically.
For a known card, REXENA:

1. **Finds and identifies** the card — powers a socket at 5 V, reads its CIS from
   attribute memory, and matches the manufacturer ID against its manifest.
2. **Configures it** — writes the card's config register (at the address its CIS
   specifies) to switch on the I/O interface, then maps the two I/O windows the
   socket provides: one for the Sound Blaster block (plus the gameport, folded in
   just below it), one for the FM/MPU ports above it.
3. **Wakes the ES1688** — enters the chip's extended mode and enables ESFM (and
   MPU, where applicable), and on combo cards writes the amp-enable so the output
   stage actually drives the speakers.
4. **Self-tests** — resets the DSP and reads back its version, so a mis-mapped port
   is reported rather than silently assumed.

Every step of that sequence was validated register-by-register against live
hardware — over COMrade — before it went into the code.

## Building

Built with **Open Watcom 1.9** (16-bit, real mode). `REXENA.EXE` is included
prebuilt; to rebuild:

```
wcc -ms REXENA.C -fo=REXENA.obj
wlink system dos name REXENA.exe file REXENA.obj
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
- **Anthropic's Claude** — reverse-engineered the register sequences, wrote the
  enabler, and drove the hardware live over COMrade. The same goal was attempted
  less than a year ago *without* COMrade: an incredibly difficult, heavily manual
  process that ultimately failed. Pairing Claude with COMrade's live feedback loop
  is what finally cracked it.
- **[The PCMCIA Sound Card Spreadsheet](https://docs.google.com/spreadsheets/d/181yznQ-DEMQRVl0X3s09NH7WD87eM3ui16isHKjIz5c)**,
  maintained by **Bondi** — a community catalog of PCMCIA sound cards and their IDs.

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

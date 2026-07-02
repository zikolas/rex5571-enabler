# Ratoc REX-5571 Enabler (`REXENA`)

A DOS point enabler for the **Ratoc REX-5571** PCMCIA sound card.

The REX-5571 is a PC Card sound device whose DOS driver/enabler is, as far as I
can tell, lost to time. Without an enabler the card never powers up or decodes
any I/O, so DOS software can't see it. `REXENA` brings the card fully to life
from a single command — **no Card Services, no Socket Services, no vendor
driver required** — by programming the PCMCIA host controller directly.

It was developed and tested on an **IBM PC110** palmtop and an **IBM ThinkPad
755C** (both Intel 82365SL-class PCICs), driven entirely remotely over a serial
link using [COMrade](https://github.com/yyzkevin/COMrade) — a big thank-you to
**yyzkevin**, whose tool made it possible to probe registers, read the CIS, and
build and test the enabler on real hardware without ever sitting in front of it.
It should work on
any machine with a register-compatible 82365 PCIC decoding the standard
index/data ports `0x3E0/0x3E1`.

## What the card actually is

Underneath, the REX-5571 is an **ESS ES1688 AudioDrive** — a Sound Blaster Pro
compatible chip (DSP v3.01) with an ESFM synthesizer and an MPU-401 UART.
The card's identity and configuration were read directly from its CIS:

```
Manufacturer : "RATOC System Inc." / "SOUND CARD 71"   (MANFID C015/0001)
Config base  : attribute address 0x400, config index 0x20
Power        : 5 V only (no Vpp, no DMA channel declared)
```

## Supported cards

Written for the REX-5571, REXENA now recognizes several ES1688-based PC Cards by
their CIS at runtime and names the one it finds:

| Card | MANFID | Notes |
|---|---|---|
| Ratoc **REX-5571** | `C015/0001` | sound only ("SOUND CARD 71") |
| Ratoc **REX-5572** | `C015/0001` | sound + SCSI-2 ("SOUND/SCSI2 CARD 72") — REXENA drives the sound half |
| Panasonic/KME **KXL-C101** | `0032/0204` | sound + CD-ROM ("KME / KXLC101"); also sold as **KXL-D20 / KXL-D745** in other regions |
| Eiger Labs **EPX-AA2000** | `0004/2000` | ES1688 sound card; COR base `0x3F0` (read from CIS). **Added from a CIS dump — not yet hardware-tested** |
| IBM **PCMCIA Portable CD-ROM Drive** | `00A4/002D` | ES1688 in an external CD-ROM box; COR base `0x200`, index `0x01`. Sound is **Windows-only** in the vendor stack — REXENA brings it up in DOS via a box **amp-enable** (below). CD-ROM/IDE half ignored; **no usable MPU** (below) |

REXENA reads each card's COR base address from its `CISTPL_CONFIG` (`0x400` for
most, `0x3F0` on the Eiger, `0x200` on the IBM box), so the config register is
written at the right place per card. The config *index* is still curated per model
in the manifest (the 5572's CIS default index selects its SCSI side; the IBM box
uses `0x01`, so it can't be auto-read).

**Box amp-enable (combo cards).** Some cards route the ES1688 through an external
power amp that's gated by one of the chip's general-purpose outputs (port `2x7h`,
bits GPO0/GPO1). The IBM box's amp is held muted until `GPO0` is driven high — the
Windows driver does this and DOS Card Services never does, which is why the box had
*no* DOS sound at all. REXENA writes the per-card value (`0x01` for the IBM box)
automatically; `/GPO=nn` overrides it for probing a new combo card.

**MPU on combo cards.** The Panasonic, Eiger and IBM entries are flagged as having
no usable MPU-401 (no MIDI connector, or — on the IBM box — the bridge doesn't
decode address bit A8, so the MPU port aliases onto the SB/IDE/gameport and merely
enabling it injects noise). REXENA auto-drops MPU for these; the RATOC 5571/5572
keep it.

The 5571 and 5572 share the same MANFID, so REXENA tells them apart by the
`VERS_1` product string. It reports what it found, for example:

```
RATOC REX-5572: socket 0 (auto)  DSP v3.01
   CIS: RATOC System Inc. SOUND/SCSI2 CARD 72  (MANFID C015/0001)
```

A card whose CIS is unreadable, or simply not in this list, can still be brought
up with **`/FORCE`** (see Usage) — the SB DSP self-test then confirms an ES1688 is
actually answering, so it won't misconfigure an empty or non-ES1688 socket.

## What works

| Feature | Address | Status |
|---|---|---|
| Sound Blaster Pro DSP | `0x220` (relocatable) | ✅ v3.01 |
| SB digitized audio (PCM) | `0x220` | ✅ direct-DAC (PIO) · ❌ via DMA |
| ESFM / AdLib FM music | SB base **and** `0x388` | ✅ |
| MPU-401 MIDI (UART) | `0x330` | ✅ (external module on MIDI-out) |
| Gameport / joystick | `0x201` | ✅ with `/JOY` — folds into window 0, so it now coexists with FM/MPU |

The MPU-401 is a MIDI *port*, not a synthesizer — the ES1688 has no onboard
wavetable. Connect an external module (MT-32, SC-55, etc.) to the card's MIDI
cable. (Tested playing to a Roland CM-32L.)

## Sound Blaster support and DMA

A PCMCIA socket on an 82365 PCIC has **no DMA channel** — the controller never
bridges the card's DMA request to the system's 8237 DMA controller — and the
REX-5571's own CIS declares no DMA either. This was verified on the hardware: the
8237 never moves for the card, and the ES1688's DMA-completion interrupt never
reaches the bus. That splits Sound Blaster support into what needs DMA and what
doesn't.

**Works (no DMA needed):**

- SB Pro **DSP** detection and register/mixer access — so games *find* the card
- **Direct-DAC (PIO) PCM** — the CPU writes each sample to the DSP (`cmd 0x10`),
  timer- or loop-paced. Real digitized effects and speech play this way.
- **ESFM/AdLib FM** music (at the SB base and `0x388`)
- **MPU-401 MIDI**

**Doesn't work (needs DMA):**

- SB **DMA-based digitized playback** — a game that programs an auto-init/single
  DMA transfer gets silence; the data never moves.
- The SB **DMA-completion IRQ** — it can only fire from a DMA terminal count, so
  it never triggers here. (Direct-DAC and FM need no interrupt at all. MPU-401
  *does* have an interrupt — for MIDI *input* — but that's a separate source, not
  this DMA IRQ; MIDI *output*, the usual case, is polled regardless.)

**In practice:** many early-'90s titles and Amiga ports push their software-mixed
samples straight to the DSP in direct mode, so they get full digital audio here —
*Another World*, *Magic Pockets*, and FastDoom's **Sound Blaster Direct** output
all work. Games that *require* DMA-based SB digitized audio (common from the
mid-'90s onward) will play FM/MIDI but be silent on digital. When a game offers a
**"Sound Blaster Direct"**-style output, pick it over plain "Sound Blaster".

## Usage

Run it once; the configuration persists in the PCIC until power-off/suspend
(the enabler is **not** a TSR). `REXENA` only programs the hardware — set the
`BLASTER` variable yourself, then
launch your SB Pro / AdLib / General-MIDI software:

```
REXENA
SET BLASTER=A220 I5 D1 T4
```

Match the `SET BLASTER` values to whatever ports/IRQ you passed to `REXENA`.

```
REXENA [/SB[=220]] [/FM[=388]] [/MPU[=330]] [/JOY] [/I=5] [/S=0..7] [/W=D000]
       [/GPO=hex] [/TONE] [/FORCE] [/OFF]
  /SB[=hex]   Sound Blaster base I/O port (always enabled; default 220)
  /FM[=hex]   dedicated AdLib/ESFM FM port (default 388)
  /MPU[=hex]  MPU-401 MIDI port (default 330; one of 300/310/320/330)
  /JOY        gameport / joystick at 201 (folds into window 0; works with /FM /MPU)
  /I=dec      IRQ (default 5; MPU requires 5/7/9/10)
  /S=dec      socket number 0..7 (default: auto-detect the card)
  /W=hex      attribute-memory window segment used to write the COR (default D000)
  /GPO=hex    diag: write this byte to the ES1688 GPO port (base+7); overrides the
              per-card amp-enable, for probing a new combo card
  /TONE       diag: leave a steady OPL tone playing (self-test of the audio path)
  /FORCE      configure the socket without the CIS identity check (requires /S)
  /OFF        power the socket down and exit
```

Each feature is a switch. SB is always on (it's the chip's control interface);
`/SB=240` just changes its base. With **no** `/FM` `/MPU` `/JOY`, the default
add-ons are **FM + MPU** — though MPU is auto-dropped on cards flagged as having
none (the Panasonic, Eiger and IBM box). Give any add-on switch and you get
exactly those, e.g. `REXENA /JOY` is SB + gameport, `REXENA /MPU` is SB + MPU only.

Before programming anything, `REXENA` identifies the card from its CIS: it powers
a socket, reads the attribute-memory CIS, matches the MANFID (and `VERS_1`)
against the [supported-cards](#supported-cards) manifest, and only proceeds on a
match — an unrecognized card is read, reported, and powered straight back down.
With no `/S` it scans every PCIC it finds and uses the first socket holding a
known card (the output tags it `(auto)`); `/S=n` selects a specific socket.

**`/FORCE`** skips that manifest check and configures the named socket directly —
for a card whose CIS is unreadable or not yet listed. It still self-tests the SB
DSP, so it won't misconfigure an empty or non-ES1688 socket. `/FORCE` requires
`/S=n`.

Each 82365 chip drives two sockets, and additional chips live at the next
index/data port pair — `0x3E0/1`, `0x3E2/3`, `0x3E4/5`, `0x3E6/7` — so socket
`N` is on the chip at `0x3E0 + (N/2)*2`, register bank `(N&1)*0x40`. The auto
scan probes all four ports (sockets 0–7) and skips any chip that isn't present.
This covers ISA 82365/ExCA controllers only; CardBus/PCI bridges use a different
(memory-mapped) interface and are out of scope.

Invalid or conflicting switches are rejected before any hardware is touched:
out-of-range I/O ports (which must stay clear of the PCIC's `0x3E0/0x3E1` and
COM1 at `0x3F8`), an illegal MPU port, or an SB base that would overlap the
shared FM/MPU window. On success the enabler self-tests by resetting the DSP at
the chosen base and reading back its version, so a port the card doesn't answer
at is reported rather than silently assumed.

### I/O windows

A PC Card socket provides only **two** I/O windows through the PCIC, and that
limit is what decides which features can run together. **Window 0 is always the
Sound Blaster block** (16 bytes at the base; the SB base follows window 0's
start). With **`/JOY`, window 0 stretches down to `0x200`** to take in the
gameport (`0x201`), which sits just below the SB base. **Window 1 is a single
contiguous range** above the base for the FM/MPU cluster:

- **MPU-401 (`0x330`)** and the **dedicated AdLib/ESFM FM port (`0x388`)** share
  window 1;
- the **gameport (`0x201`)** rides in window 0, so it coexists with FM and MPU
  (folding it in here removed the old "`/JOY` excludes `/FM`/`/MPU`" limit).

| Combination | Window 0 | Window 1 |
|---|---|---|
| SB + FM + MPU | `0x220–0x22F` | `0x330–0x389` |
| SB + FM (or MPU) | `0x220–0x22F` | `0x388–0x389` / `0x330–0x331` |
| SB + gameport + FM + MPU | `0x200–0x22F` | `0x330–0x389` |
| SB + gameport | `0x200–0x22F` | — |

`REXENA` plans the windows from your switches, rejects out-of-range or overlapping
combos up front, and prints what it actually mapped:

```
   I/O windows: win0 200-22F, win1 330-389
```

FM at the **SB base** is always present (inside window 0) regardless. Window 1 is
also where an external I/O conflict would bite (e.g. a parallel port at `0x378`
falls inside the default `0x330–0x389` FM/MPU span); if anything else decodes
inside a mapped window the two collide on the bus, so keep the SB base and window 1
clear of other active I/O.

## Building

Built with **Open Watcom 1.9** (16-bit, real mode). `REXENA.EXE` is included
prebuilt; to rebuild, run `BUILD.BAT` (or):

```
wcc -ms REXENA.C -fo=REXENA.obj
wlink system dos name REXENA.exe file REXENA.obj
```

## How it works

A PCMCIA "point enabler" does by hand what Card + Socket Services would do
automatically. `REXENA` talks straight to the 82365 PCIC and the card:

1. **Verify** the PCIC (IDREV bits 7:6 = `10`).
2. **Identify the card.** Power a socket at 5 V (`POWER` reg `0x95`: Vcc + Vpp =
   5 V, never 12 V), map a memory window onto its *attribute memory*, and walk
   the CIS for the manufacturer ID (RATOC `0xC015 / 0x0001`). (Match-or-power-down
   and the auto-scan across sockets are covered under [Usage](#usage).)
3. **Write the COR** (Configuration Option Register) at the attribute address read
   from the card's `CISTPL_CONFIG` (`0x400` for most, `0x200` on the IBM box) with
   the per-card config index (`0x20`, or `0x01` for the IBM box) — this enables the
   card's I/O interface.
4. **Map I/O windows:** window 0 = SB base (16 bytes), extended down to `0x200`
   for `/JOY` to include the gameport; window 1 = the FM/MPU cluster above the base
   (`0x330`–`0x389`) (see [I/O windows](#io-windows)).
5. **Switch to I/O-card mode** and steer the card's IREQ to the chosen IRQ.
6. **Enable ESFM / MPU-401:** enter ESS extended mode (`0xC6`) and program ESS
   mixer register `0x40` (read-modify-write) with the ESFM/legacy-decode bits and,
   unless the card is flagged no-MPU, the MPU port and IRQ.
7. **Amp-enable:** on combo cards that gate an external power amp off a GPO, write
   the per-card value to the ES1688 GPO port (`base+7`) — e.g. `0x01` for the IBM
   box, which is otherwise silent.
8. **Self-test:** SB DSP reset handshake (`0xAA`) + version, and (if MPU is on) the
   MPU-401 UART reset (`0xFE` ACK).

The whole sequence was validated register-by-register against live hardware
before being committed to code.

### Clean-room note

This implementation derives **only** from (a) the card's own CIS, read off the
hardware, and (b) public specifications: the Intel 82365SL PCIC register set,
the PCMCIA standard, and the public ESS ES1688 register model. No part of it
comes from disassembling the Ratoc vendor drivers.

## License

Copyright (c) 2026 zikolas. MIT License — see [LICENSE](LICENSE).

## Disclaimer

This is a hobby project, shared in the spirit of retro tinkering. It pokes PCMCIA
controller and sound-chip registers directly on 30-year-old hardware, so the
usual friendly warnings apply:

- **Provided as-is, with no guarantees of success** — it may or may not work with
  your card, your machine, or your particular phase of the moon.
- **No warranty, and no responsibility for any damage** — to hardware, data,
  software, or sanity — arising from its use. You run it entirely at your own risk.
- It's only been exercised on the hardware named above; anywhere else, your
  mileage may vary.

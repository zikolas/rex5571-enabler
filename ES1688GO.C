/* ES1688GO.C - a DOS point enabler for ES1688 PCMCIA sound cards.
 * Copyright (c) 2026 zikolas. MIT License - see the LICENSE file.
 *
 * Clean-room: built from the card's own CIS, the public Intel 82365SL
 * PCIC register set, and the public ESS ES1688 register model. No Card
 * Services / Socket Services required.
 *
 * The card is an ESS ES1688 (SB Pro compatible, DSP v3.01).
 * The enabler identifies it from the CIS against a small manifest of known ES1688
 * cards (RATOC REX-5571/5572, Panasonic/KME KXL-C101, Eiger EPX-AA2000, IBM
 * PCMCIA CD-ROM); an unrecognized card is
 * reported and powered back down. /FORCE skips the check (needs /S). Without /S
 * it scans every PCIC it finds (sockets 0..7).
 *
 * Features map to the 82365's TWO I/O windows. Window 0 covers the Sound
 * Blaster block (DSP, mixer, SB-base FM); with /JOY it stretches down to include
 * the gameport (0x201, just below the SB base). Window 1 covers the FM/MPU
 * cluster above the base (MPU-401 0x330, dedicated AdLib/ESFM 0x388). Folding the
 * gameport into window 0 lets /JOY run alongside /FM and /MPU.
 *
 * Build (Open Watcom 1.9, 16-bit real mode):  wcl -ms -0 -bcl=dos ES1688GO.C
 *
 * Usage: ES1688GO [/SB[=220]] [/FM[=388]] [/MPU[=330]] [/JOY]
 *               [/I=5] [/S=0..7] [/W=D000] [/GPO=hex] [/TONE] [/FORCE] [/OFF]
 *   /SB[=hex]  SB base (always enabled; default 220)
 *   /FM[=hex]  dedicated AdLib/ESFM FM port (default 388)
 *   /MPU[=hex] MPU-401 MIDI port (default 330; 300/310/320/330)
 *   /JOY       gameport/joystick at 201 (folded into window 0; ok with /FM/MPU)
 *   /I=dec     IRQ (default 5; MPU needs 5/7/9/10)
 *   /S=dec     socket 0..7 (default: auto-detect)
 *   /W=hex     attr-mem window segment for the CIS read / COR write (def D000)
 *   /GPO=hex   diag: write this byte to the ES1688 GPO port (base+7) to probe a
 *              new combo card's amp-enable; overrides the per-card manifest value
 *   /TONE      diag: leave a steady OPL tone playing (self-test of the audio path)
 *   /FORCE     configure the socket without the CIS identity check (needs /S)
 *   /OFF       power the socket down and exit
 * With no /FM /MPU /JOY, the default add-ons are FM + MPU; MPU is auto-dropped on
 * cards flagged as having none (no MIDI jack, or an aliased port). Impossible or
 * out-of-range combinations are rejected before any hardware is programmed.
 */

#include <stdio.h>
#include <conio.h>
#include <stdlib.h>
#include <i86.h>

#define PCIC_BASE 0x3E0
#define MAX_SOCKET 7

static unsigned pcic_idx = PCIC_BASE;
static unsigned sockoff  = 0;
static int      force    = 0;   /* /FORCE: skip the CIS identity check (unreadable/foreign CIS) */

static void select_socket(unsigned s)
{
    pcic_idx = PCIC_BASE + (s >> 1) * 2;
    sockoff  = (s & 1) ? 0x40 : 0x00;
}
static unsigned char rd(unsigned reg){ outp(pcic_idx, sockoff+reg); return (unsigned char)inp(pcic_idx+1); }
static void          wr(unsigned reg, unsigned val){ outp(pcic_idx, sockoff+reg); outp(pcic_idx+1, val); }
static int  controller_present(void){ return (rd(0x00) & 0xC0) == 0x80; }

static void iodelay(unsigned long n){ while(n--) (void)inp(0x80); }
#define MS(x) iodelay((unsigned long)(x)*1000UL)

static int dsp_reset(unsigned b)
{
    int i;
    outp(b + 6, 3); iodelay(10);
    outp(b + 6, 0); iodelay(40);
    for (i = 0; i < 2000; i++) if (inp(b + 0x0E) & 0x80) break;
    return (unsigned char)inp(b + 0x0A) == 0xAA;
}
static void dsp_cmd(unsigned b, unsigned char c)
{
    int i;
    for (i = 0; i < 10000; i++)
        if (!(inp(b + 0x0C) & 0x80)) { outp(b + 0x0C, c); return; }
}
static int dsp_get(unsigned b)
{
    int i;
    for (i = 0; i < 10000; i++) if (inp(b + 0x0E) & 0x80) return (unsigned char)inp(b + 0x0A);
    return -1;
}

/* TEST: play a sustained additive tone on OPL channel 0 via the SB-base FM
 * ports (b+0 addr / b+1 data). Used with /TONE to check audio output with no
 * separate program - the note keeps sounding after the enabler exits, so we can
 * toggle /GPO across runs and hear which value un-mutes the box amp. */
static void fm_tone(unsigned b)
{
    static unsigned char reg[] = {
        0x01,0x20, 0xBD,0x00,
        0x20,0x21, 0x40,0x00, 0x60,0xF0, 0x80,0x00,   /* modulator (op0) */
        0x23,0x21, 0x43,0x00, 0x63,0xF0, 0x83,0x00,   /* carrier  (op3) */
        0xC0,0x01,                                    /* additive: both operators sound */
        0xA0,0x98, 0xB0,0x32                           /* F-number + block + key-on */
    };
    int i;
    for (i = 0; i < (int)sizeof(reg); i += 2) {
        outp(b + 0, reg[i]);     iodelay(6);
        outp(b + 1, reg[i + 1]); iodelay(35);
    }
}

/* --- Manifest of known ES1688-family cards -----------------------------
 * Keyed on CISTPL_MANFID (manufacturer + card ID). The RATOC REX-5571 and
 * REX-5572 share the SAME MANFID (C015/0001), so a VERS_1 substring picks the
 * exact model. cor_index is the COR config index for the SOUND function
 * (curated: the 5572's CIS *default* index 0x22 selects its SCSI side, so we do
 * NOT read the index from the CIS - we pin the known-good sound value here).
 * The COR *base address*, however, IS read from CISTPL_CONFIG per card (see
 * g_cfgbase): most ES1688 cards put it at 0x400, but e.g. the Eiger EPX-AA2000
 * uses 0x3F0, and the COR must be written at the card's real base. */
struct known_card {
    unsigned      manf, card;    /* CISTPL_MANFID: TPLMID_MANF / TPLMID_CARD */
    char         *vers_match;    /* CISTPL_VERS_1 substring; 0 = match any */
    unsigned char cor_index;     /* COR config index for the sound function */
    int           amp_gpo;       /* box amp-enable: byte for ES1688 GPO port base+7, or -1 = none */
    int           no_mpu;        /* 1 = no usable MPU-401 (no MIDI jack, or the port aliases) */
    char         *name;
};
static struct known_card known_cards[] = {
    { 0xC015, 0x0001, "CARD 72", 0x20, -1,   0, "RATOC REX-5572"                },
    { 0xC015, 0x0001, "CARD 71", 0x20, -1,   0, "RATOC REX-5571"                },
    { 0xC015, 0x0001, 0,         0x20, -1,   0, "RATOC REX-5571/5572 (unknown)" },
    { 0x0032, 0x0204, 0,         0x20, -1,   1, "Panasonic/KME KXL-C101"        }, /* a.k.a. KXL-D20 / KXL-D745; no MIDI */
    { 0x0004, 0x2000, "EPX-AA2000", 0x20, -1, 1, "Eiger Labs EPX-AA2000"        }, /* ES1688; COR base 0x3F0; no MIDI */
    { 0x00A4, 0x002D, "CD-ROM", 0x01, 0x01,  1, "IBM Portable CD-ROM (ES1688)"  }, /* combo CD+ES1688; GPO0 gates amp; MPU aliases onto IDE/SB */
    { 0, 0, 0, 0, -1, 0, 0 }
};

/* identity of the last probed socket's card, read from its CIS */
static unsigned g_manf = 0, g_card = 0;
static unsigned g_cfgbase = 0x400;   /* COR base from CISTPL_CONFIG (default 0x400) */
static char     g_vers[80];
static struct known_card *g_match = 0;

static int contains(const char *hay, const char *needle)
{
    int i, j;
    if (!needle || !needle[0]) return 1;
    for (i = 0; hay[i]; i++) {
        for (j = 0; needle[j] && hay[i + j] == needle[j]; j++) ;
        if (!needle[j]) return 1;
    }
    return 0;
}

/* Walk the CIS in the mapped attribute window; fill g_manf/g_card/g_vers. */
static void read_cis(unsigned seg)
{
    unsigned char __far *p = (unsigned char __far *)MK_FP(seg, 0);
    unsigned off = 0;
    int guard, vi = 0, m;
    g_manf = g_card = 0; g_vers[0] = 0; g_cfgbase = 0x400;
    for (guard = 0; guard < 64; guard++) {
        unsigned char code = p[off], link;
        if (code == 0xFF) break;
        if (code == 0x00) { off += 2; continue; }   /* null tuple = 1 CIS byte */
        link = p[off + 2];
        if (code == 0x20) {                          /* CISTPL_MANFID */
            g_manf = (unsigned)p[off + 4] | ((unsigned)p[off + 6] << 8);
            g_card = (unsigned)p[off + 8] | ((unsigned)p[off + 10] << 8);
        } else if (code == 0x1A) {                   /* CISTPL_CONFIG: COR base addr */
            int rasz = (p[off + 4] & 0x03) + 1;      /* TPCC_SZ: TPCC_RADR byte count */
            g_cfgbase = p[off + 8];                   /* RADR follows TPCC_SZ, TPCC_LAST */
            if (rasz >= 2) g_cfgbase |= (unsigned)p[off + 10] << 8;
        } else if (code == 0x15) {                   /* CISTPL_VERS_1: maj,min,strings */
            for (m = 2; m < link; m++) {
                unsigned char c = p[off + 4 + 2 * m];
                if (vi < (int)sizeof(g_vers) - 1) g_vers[vi++] = c ? (char)c : ' ';
            }
            g_vers[vi] = 0;
        }
        if (link == 0xFF) break;
        off += ((unsigned)link + 2) * 2;
        if (off >= 0x3000) break;
    }
    while (vi > 0 && g_vers[vi - 1] == ' ') g_vers[--vi] = 0;   /* trim */
}

static struct known_card *identify(void)
{
    int i;
    for (i = 0; known_cards[i].name; i++)
        if (known_cards[i].manf == g_manf && known_cards[i].card == g_card
            && contains(g_vers, known_cards[i].vers_match))
            return &known_cards[i];
    return 0;
}

/* Does host segment cseg..cseg+3 pages overlap an ENABLED memory window on any
 * present socket? Read-only, so it disturbs nothing. Memory windows 0..4 = bits
 * 0..4 of the Window-Enable reg (0x06); each window's start/stop host page lives
 * in regs 0x10+w*8 .. +3. Lets the CIS probe avoid a segment another card uses. */
static int mem_win_overlaps(unsigned cseg)
{
    unsigned cs = cseg >> 8, ce = (cseg >> 8) + 3;
    unsigned chip, half, w;
    for (chip = 0; chip < 4; chip++) {
        pcic_idx = PCIC_BASE + chip * 2; sockoff = 0;
        if (!controller_present()) continue;
        for (half = 0; half < 2; half++) {
            unsigned char en;
            sockoff = half ? 0x40 : 0x00;
            en = rd(0x06);
            for (w = 0; w < 5; w++) {
                unsigned b = 0x10 + w * 8, ws, we;
                if (!(en & (1 << w))) continue;         /* window w not enabled */
                ws = ((unsigned)(rd(b + 1) & 0x0F) << 8) | rd(b);
                we = ((unsigned)(rd(b + 3) & 0x0F) << 8) | rd(b + 2);
                if (cs <= we && ws <= ce) return 1;     /* ranges overlap */
            }
        }
    }
    return 0;
}

/* Pick a 16 KB host segment for the CIS probe that no other card has mapped, so
 * the scan won't collide with an in-use card's window. Prefers 'want' (D000, or a
 * /W value); falls back through commonly-free segments. */
static unsigned find_free_window(unsigned want)
{
    static unsigned cand[] = { 0xD000, 0xCC00, 0xD400, 0xD800, 0xDC00,
                               0xE000, 0xE400, 0xE800, 0xC800, 0 };
    int i;
    if (!mem_win_overlaps(want)) return want;
    for (i = 0; cand[i]; i++) if (!mem_win_overlaps(cand[i])) return cand[i];
    return want;                                        /* nothing free - use the default */
}

/* Power a socket, map its attribute window, read the CIS; return 1 if the card
 * there is one we handle (left powered+mapped). If it's not, the socket is put
 * back the way we found it: one we powered up is powered back down, but a card
 * ALREADY running there is restored exactly - not disturbed or powered off - so
 * scanning never clobbers another card the user has going. */
static int probe_socket(unsigned memseg)
{
    unsigned start, stop, woff;
    unsigned char s03, s06, s10, s11, s12, s13, s14, s15;
    int was_on;
    if ((rd(0x01) & 0x0C) != 0x0C) return 0;             /* no card present */
    was_on = (rd(0x01) & 0x40) != 0;                     /* power up already => a card is running here */
    /* save the socket regs we borrow for the CIS read, so an already-running card
     * can be put back exactly as we found it */
    s03 = rd(0x03); s06 = rd(0x06);
    s10 = rd(0x10); s11 = rd(0x11); s12 = rd(0x12);
    s13 = rd(0x13); s14 = rd(0x14); s15 = rd(0x15);
    if (!was_on) {                                       /* only power a socket we found off */
        wr(0x02, 0x95); MS(20);
        if (!(rd(0x01) & 0x40)) { wr(0x02, 0x00); return 0; }
    }
    wr(0x03, 0x40); MS(10);                              /* reset off, memory mode */
    start = memseg >> 8;
    stop  = (memseg >> 8) + 3;
    woff  = ((unsigned)(0 - (memseg >> 8)) & 0x3FFF) | 0x4000;
    wr(0x10, start & 0xFF);  wr(0x11, (start >> 8) & 0x3F);
    wr(0x12, stop  & 0xFF);  wr(0x13, (stop  >> 8) & 0x3F);
    wr(0x14, woff  & 0xFF);  wr(0x15, (woff  >> 8) & 0xFF);
    wr(0x06, rd(0x06) | 0x01);                           /* enable mem win0 (keep other windows) */
    read_cis(memseg);                                    /* fill g_manf/g_card/g_vers */
    g_match = identify();                                 /* manifest lookup (NULL = unknown) */
    if (force || g_match) return 1;                      /* ours: leave it powered + mapped */
    /* not our card - undo the probe without harming an in-use card */
    if (was_on) {                                        /* already-running card: restore exactly, DON'T power off */
        wr(0x10, s10); wr(0x11, s11); wr(0x12, s12); wr(0x13, s13);
        wr(0x14, s14); wr(0x15, s15); wr(0x06, s06); wr(0x03, s03);
    } else {                                             /* we powered it up just to peek: power it back down */
        wr(0x06, 0x00); wr(0x03, 0x00); wr(0x02, 0x00);
    }
    return 0;
}

/* match "/NAME" or "/NAME=val" (case-insensitive); set *val to after '=' or 0 */
static int sw(const char *a, const char *name, char **val)
{
    int i;
    if (a[0] != '/' && a[0] != '-') return 0;
    for (i = 0; name[i]; i++) {
        char c = a[1 + i];
        if (c >= 'a' && c <= 'z') c -= 32;
        if (c != name[i]) return 0;
    }
    if (a[1 + i] == '\0') { *val = 0; return 1; }
    if (a[1 + i] == '=') { *val = (char *)a + 1 + i + 1; return 1; }
    return 0;
}

int main(int argc, char **argv)
{
    unsigned base = 0x220, fm = 0x388, mpu = 0x330, irq = 5, sock = 0, memseg = 0xD000;
    int off = 0, sgiven = 0, found = 0, any_chip = 0, i, mpu_ok = 0, wgiven = 0;
    int en_fm = 0, en_mpu = 0, en_joy = 0;     /* add-ons (SB is always on) */
    int gpo_set = 0, en_tone = 0;              /* TEST: amp-enable GPO hunt */
    unsigned char gpo_val = 0;
    unsigned chip, half;
    unsigned char maj, min, v, m40;
    unsigned char __far *cor;
    unsigned w0s, w0e, w1s = 0, w1e = 0, essirq, mpunib;
    int have_w1 = 0;

    for (i = 1; i < argc; i++) {
        char *a = argv[i], *vp;
        if (a[0] != '/' && a[0] != '-') continue;
        if      (sw(a, "OFF", &vp)) off = 1;
        else if (sw(a, "FORCE", &vp)) force = 1;
        else if (sw(a, "JOY", &vp)) en_joy = 1;
        else if (sw(a, "MPU", &vp)) { en_mpu = 1; if (vp) mpu = (unsigned)strtol(vp, 0, 16); }
        else if (sw(a, "SB",  &vp)) { if (vp) base = (unsigned)strtol(vp, 0, 16); }
        else if (sw(a, "FM",  &vp)) { en_fm = 1; if (vp) fm = (unsigned)strtol(vp, 0, 16); }
        else if (sw(a, "S",   &vp)) { if (vp) { sock = (unsigned)strtol(vp, 0, 10); sgiven = 1; } }
        else if (sw(a, "I",   &vp)) { if (vp) irq = (unsigned)strtol(vp, 0, 10); }
        else if (sw(a, "W",   &vp)) { if (vp) { memseg = (unsigned)strtol(vp, 0, 16); wgiven = 1; } }
        else if (sw(a, "GPO", &vp)) { if (vp) { gpo_val = (unsigned char)strtol(vp, 0, 16); gpo_set = 1; } }
        else if (sw(a, "TONE",&vp)) en_tone = 1;
        else printf("Ignoring unknown switch: %s\n", a);
    }
    if (!off && !en_fm && !en_mpu && !en_joy) { en_fm = 1; en_mpu = 1; }  /* default add-ons */

    /* --- plan the two I/O windows -----------------------------------------
     * win0 = the SB block; with /JOY it also stretches down to the gameport
     * (0x201, just below the SB base). win1 = the FM/MPU cluster above the base.
     * Folding the gameport into win0 (not spending win1 on it) lets /JOY run
     * together with /FM and /MPU. */
    w0s = base; w0e = base + 15;
    if (en_joy) w0s = 0x200;                    /* fold the gameport (0x201) into win0 */
    if (en_fm || en_mpu) {
        w1s = 0x3FF; w1e = 0x200;
        if (en_mpu) { if (mpu < w1s) w1s = mpu; if (mpu + 1 > w1e) w1e = mpu + 1; }
        if (en_fm)  { if (fm  < w1s) w1s = fm;  if (fm  + 1 > w1e) w1e = fm  + 1; }
        have_w1 = 1;
    }

    /* --- validate before touching hardware --- */
    if (sgiven && sock > MAX_SOCKET) { printf("Bad /S=%u : socket must be 0..%u\n", sock, MAX_SOCKET); return 2; }
    if (!off) {
        if (force && !sgiven) {
            printf("/FORCE needs /S=n : name the socket to configure without the CIS check.\n");
            return 2;
        }
        if (irq > 15) { printf("Bad /I=%u : IRQ must be 0..15\n", irq); return 2; }
        if (base < 0x200 || base + 15 >= 0x3E0) { printf("Bad /SB=%03X : base must be 0x200..0x3D0\n", base); return 2; }
        if (en_fm  && (fm < 0x200 || fm + 1 >= 0x3E0)) { printf("Bad /FM=%03X : 0x200..0x3DE\n", fm); return 2; }
        if (en_mpu && mpu != 0x300 && mpu != 0x310 && mpu != 0x320 && mpu != 0x330) {
            printf("Bad /MPU=%03X : must be 300/310/320/330\n", mpu); return 2;
        }
        if (have_w1 && w1e >= 0x3E0) { printf("Bad window %03X-%03X : runs into the PCIC/COM1 area\n", w1s, w1e); return 2; }
        if (have_w1 && w0s <= w1e && w1s <= w0e) {
            printf("I/O windows overlap: SB %03X-%03X and %03X-%03X. Adjust ports.\n", w0s, w0e, w1s, w1e);
            return 2;
        }
    }

    if (off) {
        select_socket(sock);
        if (!controller_present()) { printf("No PCIC controller for socket %u (port %03X)\n", sock, pcic_idx); return 1; }
        wr(0x06, 0x00); wr(0x03, 0x00); wr(0x02, 0x00);
        printf("Socket %u powered off.\n", sock);
        return 0;
    }

    /* Unless /W pinned the probe window, pick a segment no other card has mapped,
     * so the CIS read doesn't collide with (or clobber) a card already at D000. */
    if (!wgiven) {
        unsigned freeseg = find_free_window(memseg);
        if (freeseg != memseg) {
            printf("Probe window %04X in use by another card; using %04X.\n", memseg, freeseg);
            memseg = freeseg;
        }
    }

    /* identify the card; only the matching socket is left powered + mapped */
    if (sgiven) {
        select_socket(sock);
        if (!controller_present()) { printf("No PCIC controller for socket %u (port %03X)\n", sock, pcic_idx); return 1; }
        found = probe_socket(memseg);
        if (!found) {
            if (g_manf) printf("Socket %u: unrecognized card - MANFID %04X/%04X \"%s\".\n"
                               "  Use /FORCE, or add it to the manifest.\n", sock, g_manf, g_card, g_vers);
            else        printf("Socket %u: no card present/ready.\n", sock);
            return 3;
        }
    } else {
        for (chip = 0; chip < 4 && !found; chip++) {
            pcic_idx = PCIC_BASE + chip * 2; sockoff = 0;
            if (!controller_present()) continue;
            any_chip = 1;
            for (half = 0; half < 2; half++) {
                sock = chip * 2 + half;
                select_socket(sock);
                if (probe_socket(memseg)) { found = 1; break; }
            }
        }
        if (!found) {
            if (!any_chip)   printf("No 82365-class PCIC found (scanned 3E0/3E2/3E4/3E6).\n");
            else if (g_manf) printf("No known card matched (last seen MANFID %04X/%04X \"%s\").\n"
                                    "  Use /FORCE /S=n, or add it to the manifest.\n", g_manf, g_card, g_vers);
            else             printf("No known card in any socket. Use /FORCE /S=n for an unreadable CIS.\n");
            return any_chip ? 3 : 1;
        }
    }

    /* Drop MPU on cards that can't use it: the Panasonic/Eiger have no MIDI jack,
     * and the IBM box's IDE-bridge glue doesn't decode A8 so the MPU port (0x3x0)
     * aliases onto the SB/IDE/gameport (0x2x0) - enabling it just injects noise.
     * Recompute win1 without the MPU port. (Explicit /MPU is overridden too.) */
    if (g_match && g_match->no_mpu && en_mpu) {
        en_mpu = 0;
        have_w1 = 0; w1s = w1e = 0;
        if (en_fm) { w1s = fm; w1e = fm + 1; have_w1 = 1; }
        printf("Note: %s has no usable MPU-401 - MPU disabled.\n", g_match->name);
    }

    /* COR via the attribute window probe_socket left mapped. Base comes from the
     * card's CISTPL_CONFIG for a recognized card (e.g. 0x3F0 on the Eiger); for a
     * /FORCE / unreadable-CIS card we can't trust the parse, so use the 0x400
     * that's proven across the RATOC/Panasonic family. */
    cor = (unsigned char __far *)MK_FP(memseg, g_match ? g_cfgbase : 0x400);
    *cor = g_match ? g_match->cor_index : 0x20;   /* curated per-card; 0x20 for forced/unknown */

    /* I/O window 0 = SB */
    wr(0x08, w0s & 0xFF);  wr(0x09, (w0s >> 8) & 0xFF);
    wr(0x0A, w0e & 0xFF);  wr(0x0B, (w0e >> 8) & 0xFF);
    if (have_w1) {                              /* I/O window 1 = JOY or FM/MPU */
        wr(0x0C, w1s & 0xFF);  wr(0x0D, (w1s >> 8) & 0xFF);
        wr(0x0E, w1e & 0xFF);  wr(0x0F, (w1e >> 8) & 0xFF);
    }
    wr(0x07, 0x00);                             /* 8-bit */
    wr(0x03, 0x60 | 0x10 | (irq & 0x0F));       /* I/O-card mode + IRQ */
    wr(0x06, 0x40 | (have_w1 ? 0x80 : 0));      /* enable I/O window(s); mem window off */

    /* verify SB DSP */
    if (!dsp_reset(base)) { printf("Card configured but SB DSP not answering at %03X\n", base); return 1; }
    dsp_cmd(base, 0xE1);
    maj = (unsigned char)dsp_get(base);
    min = (unsigned char)dsp_get(base);

    /* ES1688 mixer reg 0x40: bit1=ESFM/OPL enable, bit0=legacy decode for the
     * gameport AND the 0x388 AdLib port, bits[4:3]=MPU port, bits[7:5]=MPU IRQ.
     * bit0 MUST be set for the dedicated 0x388 FM to decode (not just for /JOY). */
    if (en_fm || en_mpu || en_joy) {
        essirq = 0; mpunib = 0;
        if (en_mpu) {
            switch (irq) {
            case 9:  essirq = 4; break;
            case 5:  essirq = 5; break;
            case 7:  essirq = 6; break;
            case 10: essirq = 7; break;
            default: essirq = 0; break;
            }
            mpunib = (mpu & 0xF0) >> 4;
        }
        dsp_cmd(base, 0xC6);
        outp(base + 4, 0x40); iodelay(10);
        m40 = (unsigned char)inp(base + 5);
        outp(base + 4, 0x40); iodelay(10);
        outp(base + 5, (m40 & 0x04) | 0x02                       /* preserve bit2; force OPL on */
                       | ((en_fm || en_joy) ? 0x01 : 0)          /* legacy decode: 0x388 FM + gameport */
                       | (en_mpu && essirq ? ((mpunib << 3) | (essirq << 5)) : 0));
        iodelay(10);
        if (en_mpu && essirq) {
            outp(mpu + 1, 0xFF);
            for (i = 0; i < 2000; i++) if (!(inp(mpu + 1) & 0x80)) break;
            v = (unsigned char)inp(mpu);
            mpu_ok = (v == 0xFE);
        }
    }

    /* Box amp-enable. Some combo cards gate their output power amp off an ES1688
     * general-purpose output (port base+7 = 2x7h; bit0=GPO0, bit1=GPO1, "for
     * power management or other applications" per the ES1688 brief). The IBM
     * CD-ROM box needs GPO0 high (write 0x01) or it stays dead-silent - the Win95
     * driver does this, DOS Card Services never does. The manifest carries the
     * per-card value; /GPO=nn overrides it for probing a new combo card. */
    if (gpo_set)                               outp(base + 7, gpo_val);
    else if (g_match && g_match->amp_gpo >= 0) outp(base + 7, (unsigned char)g_match->amp_gpo);
    if (en_tone) fm_tone(base);

    printf("%s%s: socket %u%s  DSP v%u.%02u\n",
           g_match ? g_match->name : "Unknown card",
           g_match ? "" : " (FORCED)",
           sock, (g_match && !sgiven) ? " (auto)" : "", maj, min);
    printf("   CIS: %s  (MANFID %04X/%04X)\n", g_vers[0] ? g_vers : "unreadable", g_manf, g_card);
    if (g_match) printf("   COR @ %03Xh index %02X\n", g_cfgbase, g_match->cor_index);
    printf("   SB %03X", base);
    if (en_fm)  printf("  FM %03X", fm);
    if (en_mpu) printf("  MPU %03X%s", mpu, mpu_ok ? "" : "(?)");
    if (en_joy) printf("  JOY 201");
    printf("  IRQ %u", irq);
    printf("\n   I/O windows: win0 %03X-%03X", w0s, w0e);
    if (have_w1) printf(", win1 %03X-%03X", w1s, w1e);
    printf("\n");
    if (gpo_set)                               printf("   amp-enable: GPO %03Xh <- %02X (override)\n", base + 7, gpo_val);
    else if (g_match && g_match->amp_gpo >= 0) printf("   amp-enable: GPO %03Xh <- %02X\n", base + 7, (unsigned char)g_match->amp_gpo);
    if (en_tone)  printf("   TEST: OPL tone playing on SB-base FM\n");
    if (en_mpu && !mpu_ok) printf("   (MPU not confirmed - needs IRQ 5/7/9/10 and a 3x0 port)\n");
    return 0;
}

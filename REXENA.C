/* REXENA.C - Ratoc REX-5571 PCMCIA sound card DOS point enabler.
 * Copyright (c) 2026 zikolas. MIT License - see the LICENSE file.
 *
 * Clean-room: built from the card's own CIS, the public Intel 82365SL
 * PCIC register set, and the public ESS ES1688 register model. No Card
 * Services / Socket Services required.
 *
 * The card is an ESS ES1688 (Sound Blaster Pro compatible, DSP v3.01).
 * REXENA identifies it from the CIS against a small manifest of known ES1688
 * cards (RATOC REX-5571/5572, Panasonic/KME KXL-C101); an unrecognized card is
 * reported and powered back down. /FORCE skips the check (needs /S). Without /S
 * it scans every PCIC it finds (sockets 0..7).
 *
 * Features map to the 82365's TWO I/O windows. Window 0 is always the Sound
 * Blaster block (DSP, mixer, SB-base FM). Window 1 is one contiguous range:
 *   - the gameport (0x201) is BELOW the SB base
 *   - MPU-401 (0x330) and the dedicated AdLib/ESFM FM port (0x388) are ABOVE it
 * so window 1 can cover the gameport OR the FM/MPU cluster, never both. Hence
 * /JOY cannot be combined with /FM or /MPU. (FM at the SB base is always there.)
 *
 * Build (Open Watcom 1.9, 16-bit real mode):  wcl -ms -0 -bcl=dos REXENA.C
 *
 * Usage: REXENA [/SB[=220]] [/FM[=388]] [/MPU[=330]] [/JOY]
 *               [/I=5] [/S=0..7] [/W=D000] [/FORCE] [/OFF]
 *   /SB[=hex]  Sound Blaster base (always enabled; default 220)
 *   /FM[=hex]  dedicated AdLib/ESFM FM port (default 388)
 *   /MPU[=hex] MPU-401 MIDI port (default 330; 300/310/320/330)
 *   /JOY       gameport/joystick at 201 (excludes /FM and /MPU)
 *   /I=dec     IRQ (default 5; MPU needs 5/7/9/10)
 *   /S=dec     socket 0..7 (default: auto-detect)
 *   /W=hex     attr-mem window segment for the CIS read / COR write (def D000)
 *   /FORCE     configure the socket without the CIS identity check (needs /S)
 *   /OFF       power the socket down and exit
 * With no /FM /MPU /JOY, the default add-ons are FM + MPU. Impossible or
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

/* --- Manifest of known ES1688-family cards -----------------------------
 * Keyed on CISTPL_MANFID (manufacturer + card ID). The RATOC REX-5571 and
 * REX-5572 share the SAME MANFID (C015/0001), so a VERS_1 substring picks the
 * exact model. cor_index is the COR config index for the SOUND function
 * (curated: the 5572's CIS *default* index 0x22 selects its SCSI side, so we do
 * NOT read the index from the CIS - we pin the known-good sound value here). */
struct known_card {
    unsigned      manf, card;    /* CISTPL_MANFID: TPLMID_MANF / TPLMID_CARD */
    char         *vers_match;    /* CISTPL_VERS_1 substring; 0 = match any */
    unsigned char cor_index;     /* COR config index for the sound function */
    char         *name;
};
static struct known_card known_cards[] = {
    { 0xC015, 0x0001, "CARD 72", 0x20, "RATOC REX-5572"                },
    { 0xC015, 0x0001, "CARD 71", 0x20, "RATOC REX-5571"                },
    { 0xC015, 0x0001, 0,         0x20, "RATOC REX-5571/5572 (unknown)" },
    { 0x0032, 0x0204, 0,         0x20, "Panasonic/KME KXL-C101"        }, /* a.k.a. KXL-D20 / KXL-D745 */
    { 0, 0, 0, 0, 0 }
};

/* identity of the last probed socket's card, read from its CIS */
static unsigned g_manf = 0, g_card = 0;
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
    g_manf = g_card = 0; g_vers[0] = 0;
    for (guard = 0; guard < 64; guard++) {
        unsigned char code = p[off], link;
        if (code == 0xFF) break;
        if (code == 0x00) { off += 2; continue; }   /* null tuple = 1 CIS byte */
        link = p[off + 2];
        if (code == 0x20) {                          /* CISTPL_MANFID */
            g_manf = (unsigned)p[off + 4] | ((unsigned)p[off + 6] << 8);
            g_card = (unsigned)p[off + 8] | ((unsigned)p[off + 10] << 8);
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

static int probe_socket(unsigned memseg)
{
    unsigned start, stop, woff;
    if ((rd(0x01) & 0x0C) != 0x0C) return 0;
    wr(0x02, 0x95); MS(20);
    if (!(rd(0x01) & 0x40)) { wr(0x02, 0x00); return 0; }
    wr(0x03, 0x40); MS(10);
    start = memseg >> 8;
    stop  = (memseg >> 8) + 3;
    woff  = ((unsigned)(0 - (memseg >> 8)) & 0x3FFF) | 0x4000;
    wr(0x10, start & 0xFF);  wr(0x11, (start >> 8) & 0x3F);
    wr(0x12, stop  & 0xFF);  wr(0x13, (stop  >> 8) & 0x3F);
    wr(0x14, woff  & 0xFF);  wr(0x15, (woff  >> 8) & 0xFF);
    wr(0x06, rd(0x06) | 0x01);
    read_cis(memseg);                            /* fill g_manf/g_card/g_vers */
    g_match = identify();                         /* manifest lookup (NULL = unknown) */
    if (force || g_match) return 1;              /* /FORCE skips the accept-list gate */
    wr(0x06, 0x00); wr(0x03, 0x00); wr(0x02, 0x00);
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
    int off = 0, sgiven = 0, found = 0, any_chip = 0, i, mpu_ok = 0;
    int en_fm = 0, en_mpu = 0, en_joy = 0;     /* add-ons (SB is always on) */
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
        else if (sw(a, "W",   &vp)) { if (vp) memseg = (unsigned)strtol(vp, 0, 16); }
        else printf("Ignoring unknown switch: %s\n", a);
    }
    if (!off && !en_fm && !en_mpu && !en_joy) { en_fm = 1; en_mpu = 1; }  /* default add-ons */

    /* --- plan the two I/O windows: win0 = SB; win1 = JOY or FM/MPU --- */
    w0s = base; w0e = base + 15;
    if (en_joy) { w1s = 0x200; w1e = 0x207; have_w1 = 1; }
    else if (en_fm || en_mpu) {
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
        if (en_joy && (en_fm || en_mpu)) {
            printf("/JOY can't be combined with /FM or /MPU.\n");
            printf("Only two I/O windows: the gameport (201) is below the SB base, FM/MPU\n");
            printf("(330-388) are above it, so window 1 can't reach both. (SB-base FM still\n");
            printf("works with /JOY.) Drop /JOY, or drop /FM and /MPU.\n");
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

    /* COR (config index 0x20) via the attribute window probe_socket left mapped */
    cor = (unsigned char __far *)MK_FP(memseg, 0x400);
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

    printf("%s%s: socket %u%s  DSP v%u.%02u\n",
           g_match ? g_match->name : "Unknown card",
           g_match ? "" : " (FORCED)",
           sock, (g_match && !sgiven) ? " (auto)" : "", maj, min);
    printf("   CIS: %s  (MANFID %04X/%04X)\n", g_vers[0] ? g_vers : "unreadable", g_manf, g_card);
    printf("   SB %03X", base);
    if (en_fm)  printf("  FM %03X", fm);
    if (en_mpu) printf("  MPU %03X%s", mpu, mpu_ok ? "" : "(?)");
    if (en_joy) printf("  JOY 201");
    printf("\n   I/O windows: win0 %03X-%03X", w0s, w0e);
    if (have_w1) printf(", win1 %03X-%03X", w1s, w1e);
    printf("\n");
    if (en_mpu && !mpu_ok) printf("   (MPU not confirmed - needs IRQ 5/7/9/10 and a 3x0 port)\n");
    return 0;
}

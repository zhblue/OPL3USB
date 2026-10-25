/*
 * vgmchip.h - chip handling for the OPL3USB player.
 *
 * A VGM file records which chip the music was made for.  The command
 * numbers have been stable since the early VGM versions:
 *
 *   0x50       SN76489 PSG
 *   0x51       YM2413 (OPLL)
 *   0x52/0x53  YM2612 (FM) ports 0/1
 *   0x54       YM2151
 *   0x55       YM2203
 *   0x56/0x57  YM2608
 *   0x58/0x59  YM2610
 *   0x5A       YM3812 (OPL2)
 *   0x5B       YM3526 (OPL1)
 *   0x5C       Y8950
 *   0x5D       YMZ280B
 *   0x5E/0x5F  YMF262 (OPL3) ports 0/1
 *
 * This is independent of the VGM version field.  In particular, Tyrian's
 * VGM 1.51 files use 0x5A for OPL2, while many YM2612 files use 0x52/0x53.
 *
 * The board carries a YMF262 (OPL3).  Its A1 pin selects the register bank, so
 * the "bank" bit of the Nuke protocol (which the VGM command decides) already
 * picks the right register set: OPL2 data goes to the YM3812 compatible bank,
 * OPL3 data to the OPL3 bank.
 *
 * The clock of the two chips is different and that matters:
 *
 *   YM3812 (OPL2) : 3.579545 MHz
 *   YMF262 (OPL3) : 14.31818 MHz  (= 4 x 3.579545 MHz)
 *
 * The YMF262 divides its clock by four in OPL2 mode, so OPL2 data is played
 * with the original YM3812 timing - but only if the board actually feeds the
 * chip 14.31818 MHz.  If the board runs the chip at 3.579545 MHz (a missing
 * 4x clock generator in the OPL3LPT modification) everything sounds a factor
 * four too low.  The block field of the OPL2 F-number registers can shift the
 * pitch by whole octaves, which is the software lever for a chip that runs
 * with half the clock it should have.
 *
 * OPL1 (YM3526) files use their own register map and are translated here into
 * the OPL2 register set.  YM2413, YM2612/YM2203-family FM, AY8910, SN76489 and
 * NES APU streams are translated by vgmconvert.h while playback is running.
 */

#ifndef VGMCHIP_H
#define VGMCHIP_H

#include <string.h>

#define VGMCHIP_MAXOUT 160

#include "vgmconvert.h"

/* Official VGM command bytes for the chips we understand. */
#define VGM_CHIP_SN76489    0x50   /* SN76489 PSG                   */
#define VGM_CHIP_YM2413     0x51   /* YM2413 (OPLL)                 */
#define VGM_CHIP_YM2612_0   0x52   /* YM2612 port 0                  */
#define VGM_CHIP_YM2612_1   0x53   /* YM2612 port 1                  */
#define VGM_CHIP_YM2203     0x55   /* YM2203                         */
#define VGM_CHIP_YM2608_0   0x56   /* YM2608 port 0                  */
#define VGM_CHIP_YM2608_1   0x57   /* YM2608 port 1                  */
#define VGM_CHIP_YM2610_0   0x58   /* YM2610 port 0                  */
#define VGM_CHIP_YM2610_1   0x59   /* YM2610 port 1                  */
#define VGM_CHIP_YM3812     0x5A   /* YM3812 (OPL2)                  */
#define VGM_CHIP_YM3526     0x5B   /* YM3526 (OPL1)                  */
#define VGM_CHIP_Y8950      0x5C   /* Y8950                          */
#define VGM_CHIP_OPL3_PORT0 0x5E   /* YMF262 port 0                 */
#define VGM_CHIP_OPL3_PORT1 0x5F   /* YMF262 port 1                 */
#define VGM_CHIP_AY8910     0xA0   /* AY-3-8910                     */
#define VGM_CHIP_NES_APU    0xB4   /* NES APU (RP2A03)              */

typedef struct {
    unsigned char use_opl2;     /* YM3812 data seen   */
    unsigned char use_opl3;     /* YMF262 data seen   */
    unsigned char use_opl1;     /* YM3526 data seen   */
    unsigned char use_fm;       /* YM2612 data seen   */
    unsigned char use_y8950;    /* Y8950 data seen    */
    unsigned char use_opll;     /* YM2413 data seen   */
    unsigned char use_ay;       /* AY8910 data seen   */
    unsigned char use_sn;       /* SN76489 data seen  */
    unsigned char use_nes;      /* NES APU data seen  */
    unsigned char opn_kind;     /* 1=YM2612, 2=YM2203, 3=YM2608, 4=YM2610 */
    /* OPL1 -> OPL2 translation state */
    unsigned char opl1_block[6]; /* OPL1 block per channel  */
    unsigned char opl1_fnhi[6];  /* OPL1 F-number high     */
    unsigned char opl1_written[6];/* last OPL2 value sent  */
    unsigned char opl1_valid[6];
    /* whole octaves the OPL2 pitch is shifted by (clock correction) */
    int octave;
    /* stream converters for source chips that YMF262 cannot emulate directly */
    VgmConverterState conv;
} VgmChip;

static void vgmchip_init(VgmChip *c, int octave)
{
    memset(c, 0, sizeof(*c));
    c->octave = octave;
}

static unsigned int vgmchip_u32le(const unsigned char *p)
{
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8) |
           ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
}

/*
 * Fill the chip flags from the clocks in the VGM header.  The command stream
 * is the final authority, but the header is available before playback starts
 * and is what lets the GUI show the chip name immediately.
 */
static void vgmchip_detect_header(VgmChip *c, const unsigned char *header, int len)
{
    unsigned int clock;
    unsigned int version = vgmchip_u32le(header + 0x08);
    unsigned int dataoff = version >= 0x150 ?
        vgmchip_u32le(header + 0x34) + 0x34 : 0x40;

    // Only interpret a header field if it lies before the command stream.
    // Some VGM 1.51 writers use data offset 0x40 or 0x80, so a fixed 0x80
    // header read would otherwise treat command bytes as chip clocks.
    if (dataoff < 0x40) dataoff = 0x40;
    if (len >= 0x10 && dataoff >= 0x10) {
        clock = vgmchip_u32le(header + 0x0C) & 0x7FFFFFFFu; /* SN76489 */
        if (clock) { c->use_sn = 1; c->conv.clock_sn = clock; }
    }
    if (len >= 0x14 && dataoff >= 0x14) {
        clock = vgmchip_u32le(header + 0x10) & 0x7FFFFFFFu; /* YM2413 */
        if (clock) { c->use_opll = 1; c->conv.clock_opll = clock; }
    }
    if (len >= 0x30 && dataoff >= 0x30) {
        clock = vgmchip_u32le(header + 0x2C) & 0x7FFFFFFFu; /* YM2612 */
        if (clock) { c->use_fm = 1; c->opn_kind = 1; c->conv.clock_fm = clock; }
    }
    if (version < 0x151) return;
    if (len >= 0x48 && dataoff >= 0x48) {
        clock = vgmchip_u32le(header + 0x44);               /* YM2203 */
        if (clock) { c->use_fm = 1; c->opn_kind = 2; c->conv.clock_ym2203 = clock; }
    }
    if (len >= 0x4c && dataoff >= 0x4c) {
        clock = vgmchip_u32le(header + 0x48);               /* YM2608 */
        if (clock) { c->use_fm = 1; c->opn_kind = 3; }
    }
    if (len >= 0x50 && dataoff >= 0x50) {
        clock = vgmchip_u32le(header + 0x4C);               /* YM2610 */
        if (clock) { c->use_fm = 1; c->opn_kind = 4; }
    }
    if (len >= 0x78 && dataoff >= 0x78) {
        clock = vgmchip_u32le(header + 0x74);               /* AY8910 */
        if (clock) { c->use_ay = 1; c->conv.clock_ay = clock; }
    }
    if (len >= 0x54 && dataoff >= 0x54) {
        clock = vgmchip_u32le(header + 0x50) & 0x7FFFFFFFu; /* YM3812 */
        if (clock) c->use_opl2 = 1;
    }
    if (len >= 0x58 && dataoff >= 0x58) {
        clock = vgmchip_u32le(header + 0x54);               /* YM3526 */
        if (clock) c->use_opl1 = 1;
    }
    if (len >= 0x5C && dataoff >= 0x5C) {
        clock = vgmchip_u32le(header + 0x58);               /* Y8950 */
        if (clock) c->use_y8950 = 1;
    }
    if (len >= 0x60 && dataoff >= 0x60) {
        clock = vgmchip_u32le(header + 0x5C);               /* YMF262 */
        if (clock) c->use_opl3 = 1;
    }
    if (version >= 0x161 && len >= 0x88 && dataoff >= 0x88) {
        clock = vgmchip_u32le(header + 0x84);               /* NES APU */
        if (clock) { c->use_nes = 1; c->conv.clock_nes = clock; }
    }
}

static int vgmchip_is_write(unsigned char cmd)
{
    switch (cmd) {
        case VGM_CHIP_SN76489:
        case VGM_CHIP_YM2413:
        case VGM_CHIP_YM2612_0:
        case VGM_CHIP_YM2612_1:
        case VGM_CHIP_YM2203:
        case VGM_CHIP_YM2608_0:
        case VGM_CHIP_YM2608_1:
        case VGM_CHIP_YM2610_0:
        case VGM_CHIP_YM2610_1:
        case VGM_CHIP_YM3812:
        case VGM_CHIP_YM3526:
        case VGM_CHIP_Y8950:
        case VGM_CHIP_OPL3_PORT0:
        case VGM_CHIP_OPL3_PORT1:
        case VGM_CHIP_AY8910:
        case VGM_CHIP_NES_APU:
            return 1;
        default:
            return 0;
    }
}

static int vgmchip_write_len(unsigned char cmd)
{
    if (!vgmchip_is_write(cmd)) return 0;
    return cmd == VGM_CHIP_SN76489 ? 2 : 3;
}

/* Which chip does this command belong to?  Returns 0 for "not a chip write". */
static int vgmchip_classify(const VgmChip *c, unsigned char cmd)
{
    (void)c;
    switch (cmd) {
        case VGM_CHIP_YM3812:    return 1;        /* YM3812 (OPL2)  */
        case VGM_CHIP_YM3526:    return 2;        /* YM3526 (OPL1)  */
        case VGM_CHIP_OPL3_PORT0:
        case VGM_CHIP_OPL3_PORT1:return 3;        /* YMF262 (OPL3)  */
        case VGM_CHIP_YM2612_0:
        case VGM_CHIP_YM2612_1:
        case VGM_CHIP_YM2203:
        case VGM_CHIP_YM2608_0:
        case VGM_CHIP_YM2608_1:
        case VGM_CHIP_YM2610_0:
        case VGM_CHIP_YM2610_1:  return 4;        /* OPN FM         */
        case VGM_CHIP_Y8950:     return 5;        /* Y8950          */
        case VGM_CHIP_YM2413:    return 6;        /* YM2413 (OPLL)  */
        case VGM_CHIP_AY8910:    return 7;        /* AY8910         */
        case VGM_CHIP_SN76489:   return 8;        /* SN76489        */
        case VGM_CHIP_NES_APU:   return 9;        /* NES APU        */
        default:                 return 0;
    }
}

static unsigned int vgmchip_mask(const VgmChip *c)
{
    return (unsigned int)c->use_opl2 | ((unsigned int)c->use_opl3 << 1) |
           ((unsigned int)c->use_opl1 << 2) | ((unsigned int)c->use_fm << 3) |
           ((unsigned int)c->use_y8950 << 4) | ((unsigned int)c->use_opll << 5) |
           ((unsigned int)c->use_ay << 6) | ((unsigned int)c->use_sn << 7) |
           ((unsigned int)c->use_nes << 8);
}

static void vgmchip_note(VgmChip *c, unsigned char cmd)
{
    switch (vgmchip_classify(c, cmd)) {
        case 1: c->use_opl2 = 1; break;
        case 2: c->use_opl1 = 1; break;
        case 3: c->use_opl3 = 1; break;
        case 4:
            c->use_fm = 1;
            if (cmd == VGM_CHIP_YM2612_0 || cmd == VGM_CHIP_YM2612_1) c->opn_kind = 1;
            else if (cmd == VGM_CHIP_YM2203) c->opn_kind = 2;
            else if (cmd == VGM_CHIP_YM2608_0 || cmd == VGM_CHIP_YM2608_1) c->opn_kind = 3;
            else if (cmd == VGM_CHIP_YM2610_0 || cmd == VGM_CHIP_YM2610_1) c->opn_kind = 4;
            break;
        case 5: c->use_y8950 = 1; break;
        case 6: c->use_opll = 1; break;
        case 7: c->use_ay = 1; break;
        case 8: c->use_sn = 1; break;
        case 9: c->use_nes = 1; break;
        default: break;
    }
}

/* OPL1 channel -> OPL2 register (YM3812 map, 6 channels) */
static const unsigned char vgm_opl1_fnlo[6] = { 0x20, 0x22, 0x24, 0x21, 0x23, 0x25 };
static const unsigned char vgm_opl1_fnhi[6] = { 0x30, 0x32, 0x34, 0x31, 0x33, 0x35 };

/* Build the OPL2 F-number high byte (block + F-number high) of a channel. */
static unsigned char vgm_opl1_merge(VgmChip *c, int ch)
{
    unsigned char block = c->opl1_block[ch] & 0x03;
    unsigned char fnhi = c->opl1_fnhi[ch] & 0x07;
    /* OPL2: bits 2-5 = block, bits 0-1 = F-number high (3 bits in OPL1) */
    /* The OPL1 F-number is 10 bit, just like the OPL2 one: 8 bit in the low
       register and 2 bit in the high register, which also carries the block. */
    unsigned char value = (unsigned char)(((block & 0x03) << 2) | (fnhi & 0x03));
    int shift = c->octave;
    if (shift) {
        int block2 = ((value >> 2) & 0x03) + shift;
        if (block2 < 0) block2 = 0;
        if (block2 > 3) block2 = 3;
        value = (unsigned char)((value & 0x03) | (block2 << 2));
    }
    return value;
}

/*
 * Translate one VGM register write into the writes that have to be sent to
 * the board.  Returns the number of writes, they are stored as
 * {command, address, data} triples in out[].
 */
static int vgmchip_translate(VgmChip *c, unsigned char cmd, unsigned char addr,
                             unsigned char data, unsigned char out[][3])
{
    int n = 0;
    int kind = vgmchip_classify(c, cmd);
    VgmConvOutput co;
    co.out = out;
    co.n = 0;

    if (kind == 4) {
        int port = (cmd == VGM_CHIP_YM2612_1 || cmd == VGM_CHIP_YM2608_1 ||
                    cmd == VGM_CHIP_YM2610_1) ? 1 : 0;
        return vgmconv_opn_write(&c->conv, &co, port, addr, data);
    }
    if (kind == 6) return vgmconv_opll_write(&c->conv, &co, addr, data);
    if (kind == 7) return vgmconv_ay_write(&c->conv, &co, addr, data);
    if (kind == 8) return vgmconv_sn_write(&c->conv, &co, addr);
    if (kind == 9) return vgmconv_nes_write(&c->conv, &co, addr, data);

    if (kind == 2) {
        /* ---- OPL1 (YM3526) -> OPL2 (YM3812) -------------------------- */
        if (addr <= 0x05) {                 /* control registers */
            if (addr == 0x05) {
                /* bit 6 of the OPL1 is the rhythm enable, OPL2 uses bit 7 */
                unsigned char v = (unsigned char)((data & 0x3F) | ((data & 0x40) << 1));
                out[n][0] = VGM_CHIP_YM3812; out[n][1] = 0x05; out[n][2] = v; n++;
            } else if (addr == 0x01) {
                /* bit 5 is the OPL1 "test" bit, it means something else on OPL2 */
                out[n][0] = VGM_CHIP_YM3812; out[n][1] = 0x01; out[n][2] = (unsigned char)(data & 0xDF); n++;
            } else {
                out[n][0] = VGM_CHIP_YM3812; out[n][1] = addr; out[n][2] = data; n++;
            }
            return n;
        }
        if (addr >= 0x08 && addr <= 0x0D) {     /* F-number low, channels 1-6 */
            int ch = addr - 0x08;
            out[n][0] = VGM_CHIP_YM3812; out[n][1] = vgm_opl1_fnlo[ch]; out[n][2] = data; n++;
            return n;
        }
        if (addr >= 0x10 && addr <= 0x15) {     /* F-number high */
            int ch = addr - 0x10;
            c->opl1_fnhi[ch] = data;
            out[n][0] = VGM_CHIP_YM3812; out[n][1] = vgm_opl1_fnhi[ch];
            out[n][2] = vgm_opl1_merge(c, ch); n++;
            c->opl1_written[ch] = out[n - 1][2];
            c->opl1_valid[ch] = 1;
            return n;
        }
        if (addr >= 0x16 && addr <= 0x1B) {     /* block (octave) */
            int ch = addr - 0x16;
            c->opl1_block[ch] = data;
            if (c->opl1_valid[ch]) {
                out[n][0] = VGM_CHIP_YM3812; out[n][1] = vgm_opl1_fnhi[ch];
                out[n][2] = vgm_opl1_merge(c, ch); n++;
                c->opl1_written[ch] = out[n - 1][2];
            }
            return n;
        }
        if (addr >= 0x20 && addr <= 0x25) {     /* instrument, channels 1-6 */
            int ch = addr - 0x20;
            unsigned char level = (unsigned char)((data >> 6) & 0x03);
            unsigned char fb    = (unsigned char)(data & 0x03);
            unsigned char ar    = (unsigned char)((data >> 2) & 0x03); /* attack/decay/sustain/release */
            /* operator register: MULT = 1 (normal octave), no AM/VIB/EG */
            out[n][0] = VGM_CHIP_YM3812; out[n][1] = (unsigned char)(0x40 + ch);
            out[n][2] = 0x01; n++;
            /* instrument register: level, envelope rates and feedback */
            out[n][0] = VGM_CHIP_YM3812; out[n][1] = (unsigned char)(0x50 + ch);
            out[n][2] = (unsigned char)(((level * 5) << 4) | ((ar & 0x03) << 2) | (fb & 0x03));
            n++;
            return n;
        }
        if (addr == 0x36 || addr == 0x37) {     /* feedback / connection */
            int base = (addr == 0x36) ? 0 : 3;
            for (int i = 0; i < 3; i++) {
                int ch = base + i;
                unsigned char v = (unsigned char)(((data >> (6 - i * 2)) & 0x03));
                out[n][0] = VGM_CHIP_YM3812; out[n][1] = (unsigned char)(0xC0 + ch);
                out[n][2] = (unsigned char)(v & 0x01); n++;
            }
            return n;
        }
        if (addr >= 0x38 && addr <= 0x3F) {     /* waveform, channels 1-6 */
            int ch = addr - 0x38;
            out[n][0] = VGM_CHIP_YM3812; out[n][1] = (unsigned char)(0xC0 + ch);
            out[n][2] = (unsigned char)((data & 0x07) << 1); n++;
            return n;
        }
        if (addr == 0x0E || addr == 0x0F) {     /* rhythm control */
            if (addr == 0x0E) {
                out[n][0] = VGM_CHIP_YM3812; out[n][1] = 0xBD;
                out[n][2] = (unsigned char)((data & 0x01) ? 0x20 : 0x00); n++;
            } else {
                out[n][0] = VGM_CHIP_YM3812; out[n][1] = 0xBD;
                out[n][2] = (unsigned char)((data & 0x0F) << 4); n++;
            }
            return n;
        }
        return 0;                              /* not translatable */
    }

    {
        if (kind == 5 || kind == 0) {
            /* FM, OPLL, Y8950 and the other chips have their own register
               maps, they cannot be played on an OPL3 board. */
            return 0;
        }
        if (kind == 3) {
            /* Preserve the YMF262 port: 0x5E is bank 0, 0x5F is bank 1. */
            out[n][0] = cmd; out[n][1] = addr; out[n][2] = data; n++;
            return n;
        }
        /* OPL2 data goes to the OPL2 register bank */
        cmd = VGM_CHIP_YM3812;
    }

    /* ---- OPL2 data --------------------------------------------------- */
    if (c->octave) {
        /* shift the pitch by whole octaves (clock correction) */
        if (addr >= 0x30 && addr <= 0x3F) {
            unsigned char value = data;
            int block = (value >> 2) & 0x03;
            block += c->octave;
            if (block < 0) block = 0;
            if (block > 3) block = 3;
            value = (unsigned char)((value & 0x03) | (block << 2));
            out[n][0] = cmd; out[n][1] = addr; out[n][2] = value; n++;
            return n;
        }
    }
    out[n][0] = cmd; out[n][1] = addr; out[n][2] = data; n++;
    return n;
}

/* A short description of the chips a file uses, for the UI and the log. */
static const char *vgmchip_desc(VgmChip *c, char *buf, int len)
{
    char *p = buf;
    int first = 1;
    p[0] = 0;
#define ADD(str) do { \
        if (!first) { if (p + strlen(str) + 2 < buf + len) { *p++ = '+'; *p = 0; } } \
        first = 0; \
        if (p + strlen(str) + 1 < buf + len) { strcpy(p, str); p += strlen(str); *p = 0; } \
    } while (0)
    if (c->use_opl2) ADD("OPL2(YM3812)");
    if (c->use_opl3) ADD("OPL3(YMF262)");
    if (c->use_opl1) ADD("OPL1(YM3526)");
    if (c->use_fm) {
        if (c->opn_kind == 2) ADD("YM2203(FM)->OPL3");
        else if (c->opn_kind == 3) ADD("YM2608(FM)->OPL3");
        else if (c->opn_kind == 4) ADD("YM2610(FM)->OPL3");
        else ADD("YM2612(FM)->OPL3");
    }
    if (c->use_ay)   ADD("AY8910->OPL3");
    if (c->use_sn)   ADD("SN76489->OPL3");
    if (c->use_nes)  ADD("NES APU->OPL3");
    if (c->use_opll) ADD("YM2413(OPLL)->OPL3");
    if (c->use_y8950) ADD("Y8950");
    if (first) ADD("none");
#undef ADD
    return buf;
}

#endif /* VGMCHIP_H */

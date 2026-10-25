/*
 * vgmconvert.h - real-time register-stream converters for YMF262.
 *
 * The original VGM is still played directly. Register writes for supported
 * source chips are translated into YMF262 (OPL3) register writes as they are
 * read, so no external converter or temporary file is needed.
 *
 * NES APU is based on nes2opl3. AY8910 and SN76489 follow the mapping ideas
 * used by vgm-conv. These are intentionally lightweight real-time mappings.
 */

#ifndef VGMCONVERT_H
#define VGMCONVERT_H

#include <string.h>

#define VGMCONV_OPL_CLOCK 14318180.0
#define VGMCONV_NES_BASE 15
#define VGMCONV_NES_COPY_BASE 13
#define VGMCONV_AY_BASE 9
#define VGMCONV_SN_BASE 9

typedef struct {
    unsigned char (*out)[3];
    int n;
} VgmConvOutput;

typedef struct {
    unsigned char r20, r40, r60, r80, rE0;
} VgmOplSlot;

typedef struct {
    unsigned int clock_sn;
    unsigned int clock_opll;
    unsigned int clock_fm;
    unsigned int clock_ay;
    unsigned int clock_ym2203;
    unsigned int clock_nes;

    unsigned char shadow[512];
    unsigned char shadow_valid[512];
    int opl3_ready;

    int nes_ready;
    int nes_pulse_duty[2];
    int nes_pulse_vol[2];
    int nes_pulse_timer[2];
    int nes_pulse_timer_valid[2];
    int nes_pulse_enabled[2];
    int nes_pulse_on[2];
    int nes_tri_timer;
    int nes_tri_enabled;
    int nes_tri_on;
    int nes_tri_timer_valid;
    int nes_noise_vol;
    int nes_noise_mode;
    int nes_noise_period;
    int nes_noise_enabled;
    int nes_noise_on;

    int ay_ready;
    unsigned char ay_regs[256];

    int opll_ready;
    unsigned char opll_regs[256];

    int opn_ready;
    unsigned char opn_regs[2][256];
    int opn_key[6];

    int sn_ready;
    unsigned char sn_fdiv[4];
    unsigned char sn_vol[4];
    int sn_ch;
    int sn_type;
    int sn_noise_ctl;
} VgmConverterState;

static void vgmconv_emit(VgmConverterState *s, VgmConvOutput *o,
                         int reg, int data)
{
    reg &= 0x1ff;
    data &= 0xff;
    if (s->shadow_valid[reg] && s->shadow[reg] == data) return;
    if (o->n >= VGMCHIP_MAXOUT) return;
    s->shadow_valid[reg] = 1;
    s->shadow[reg] = (unsigned char)data;
    o->out[o->n][0] = (unsigned char)(reg >= 0x100 ? 0x5F : 0x5E);
    o->out[o->n][1] = (unsigned char)(reg & 0xff);
    o->out[o->n][2] = (unsigned char)data;
    o->n++;
}

static void vgmconv_opl3_init(VgmConverterState *s, VgmConvOutput *o)
{
    if (s->opl3_ready) return;
    s->opl3_ready = 1;
    vgmconv_emit(s, o, 0x105, 0x01);
    vgmconv_emit(s, o, 0x104, 0x00);
    vgmconv_emit(s, o, 0x0BD, 0xC0);
}

static void vgmconv_voice_regs(int voice, int *mod, int *car,
                               int *c0, int *a0, int *b0)
{
    int bank = (voice >= 9) ? 1 : 0;
    int local = voice % 9;
    int base = bank ? 0x100 : 0;
    int m = 8 * (local / 3) + (local % 3);
    *mod = base + 0x20 + m;
    *car = *mod + 3;
    *c0 = base + 0xC0 + local;
    *a0 = base + 0xA0 + local;
    *b0 = base + 0xB0 + local;
}

static void vgmconv_write_voice(VgmConverterState *s, VgmConvOutput *o,
                                int voice, VgmOplSlot mod, VgmOplSlot car,
                                int c0)
{
    int mo, ca, cc, a0, b0;
    vgmconv_voice_regs(voice, &mo, &ca, &cc, &a0, &b0);
    vgmconv_emit(s, o, mo, mod.r20);
    vgmconv_emit(s, o, ca, car.r20);
    vgmconv_emit(s, o, mo + 0x20, mod.r40);
    vgmconv_emit(s, o, ca + 0x20, car.r40);
    vgmconv_emit(s, o, mo + 0x40, mod.r60);
    vgmconv_emit(s, o, ca + 0x40, car.r60);
    vgmconv_emit(s, o, mo + 0x60, mod.r80);
    vgmconv_emit(s, o, ca + 0x60, car.r80);
    vgmconv_emit(s, o, mo + 0xC0, mod.rE0);
    vgmconv_emit(s, o, ca + 0xC0, car.rE0);
    vgmconv_emit(s, o, cc, c0);
}

static void vgmconv_voice_freq(VgmConverterState *s, VgmConvOutput *o,
                               int voice, int block, int fnum, int key)
{
    int mo, ca, cc, a0, b0;
    vgmconv_voice_regs(voice, &mo, &ca, &cc, &a0, &b0);
    vgmconv_emit(s, o, a0, fnum & 0xff);
    vgmconv_emit(s, o, b0, (key ? 0x20 : 0) | ((block & 7) << 2) |
                               ((fnum >> 8) & 3));
}

static void vgmconv_voice_key(VgmConverterState *s, VgmConvOutput *o,
                              int voice, int key)
{
    int mo, ca, cc, a0, b0;
    int v;
    vgmconv_voice_regs(voice, &mo, &ca, &cc, &a0, &b0);
    v = s->shadow_valid[b0] ? s->shadow[b0] : 0;
    if (key) v |= 0x20; else v &= ~0x20;
    vgmconv_emit(s, o, b0, v);
}

static void vgmconv_freq(double freq, int *block, int *fnum)
{
    int b = 0;
    double fn;
    if (freq < 1.0) freq = 1.0;
    if (freq > 12000.0) freq = 12000.0;
    fn = freq * 1048576.0 * 288.0 / VGMCONV_OPL_CLOCK;
    while (fn > 1023.0 && b < 7) { b++; fn *= 0.5; }
    if (fn < 1.0) fn = 1.0;
    if (fn > 1023.0) fn = 1023.0;
    *block = b;
    *fnum = (int)(fn + 0.5);
}

#define NES_MOD_OFF(v) ((v) < 3 ? (v) : (v) + 1)
#define NES_CAR_OFF(v) (NES_MOD_OFF(v) + 3)

static void vgmconv_nes_voice_setup(VgmConverterState *s, VgmConvOutput *o,
                                    int voice, int wave_mod, int wave_car,
                                    int feedback)
{
    /* Additive mode: both operators contribute to the output.  Together
       with the duplicated pulse voice this gives about 4x amplitude without
       FM feedback noise. */
    VgmOplSlot mod = {0x01, 0x3F, 0xF0, 0xFF, (unsigned char)wave_mod};
    VgmOplSlot car = {0x01, 0x3F, 0xE0, 0xFF, (unsigned char)wave_car};
    (void)feedback;
    vgmconv_write_voice(s, o, voice, mod, car, 0x31);
}

static void vgmconv_nes_tl(VgmConverterState *s, VgmConvOutput *o,
                           int voice, int vol15)
{
    int mo, ca, cc, a0, b0;
    /* NES 0..15 is not a 4 dB/step control.  A gentler approximation keeps
       the melody audible at the common volumes 3..7. */
    int tl = (15 - vol15) * 2;
    if (tl < 0) tl = 0;
    if (tl > 63) tl = 63;
    vgmconv_voice_regs(voice, &mo, &ca, &cc, &a0, &b0);
    vgmconv_emit(s, o, ca + 0x20, tl);
}

static void vgmconv_nes_pulse_tl(VgmConverterState *s, VgmConvOutput *o,
                                 int voice, int vol15)
{
    int mo, ca, cc, a0, b0;
    /* The two operators are additive and carry the same square wave, so this
       pairs them at one level.  The 9-step boost (6.75 dB) is tuned against
       the NES square: measured 2.96 dB louder than a sine at equal TL, plus
       margin so square peaks do not clip when several channels coincide. */
    int tl = (15 - vol15) * 2 - 8;
    if (tl < 0) tl = 0;
    if (tl > 63) tl = 63;
    vgmconv_voice_regs(voice, &mo, &ca, &cc, &a0, &b0);
    vgmconv_emit(s, o, mo + 0x20, tl);
    vgmconv_emit(s, o, ca + 0x20, tl);
}

static void vgmconv_nes_wave(VgmConverterState *s, VgmConvOutput *o,
                             int voice, int wave)
{
    int mo, ca, cc, a0, b0;
    vgmconv_voice_regs(voice, &mo, &ca, &cc, &a0, &b0);
    vgmconv_emit(s, o, mo + 0xC0, wave & 7);
    vgmconv_emit(s, o, ca + 0xC0, wave & 7);
}

static void vgmconv_nes_update_pulse(VgmConverterState *s, VgmConvOutput *o, int i)
{
    /* A timer value of 0 is an ultrasonic 111.8 kHz square on the NES and the
       timer is not complete until the music engine has written it, so never
       key a pulse voice before that: the old code keyed at timer 0 and
       produced a short 6 kHz beep at the start of every note. */
    int active = s->nes_pulse_enabled[i] && s->nes_pulse_vol[i] > 0 &&
                 s->nes_pulse_timer_valid[i] &&
                 s->nes_pulse_timer[i] > 0 && s->nes_pulse_timer[i] <= 0x7ff;
    /* NES pulse duties (12.5/25/50/75 %) are all square-ish; OPL3 wf6 is the
       only waveform that is DC-free and keeps a strong fundamental plus odd
       harmonics.  A sine here made the melody sound muffled, and the
       quarter-sine (wf3) has DC and a dominant second harmonic (octave up). */
    static const unsigned char pulse_wave = 6;
    int voices[2] = { VGMCONV_NES_BASE + i, VGMCONV_NES_COPY_BASE + i };
    if (active) {
        double freq = (s->clock_nes ? s->clock_nes : 1789773u) /
                      (16.0 * (s->nes_pulse_timer[i] + 1));
        int block, fnum, k;
        vgmconv_freq(freq, &block, &fnum);
        for (k = 0; k < 2; k++) {
            vgmconv_voice_freq(s, o, voices[k], block, fnum, 1);
            vgmconv_nes_pulse_tl(s, o, voices[k], s->nes_pulse_vol[i]);
            vgmconv_nes_wave(s, o, voices[k], pulse_wave);
        }
        s->nes_pulse_on[i] = 1;
    } else if (s->nes_pulse_on[i]) {
        int k;
        for (k = 0; k < 2; k++) vgmconv_voice_key(s, o, voices[k], 0);
        s->nes_pulse_on[i] = 0;
    }
}

static void vgmconv_nes_update_triangle(VgmConverterState *s, VgmConvOutput *o)
{
    if (s->nes_tri_enabled && s->nes_tri_timer_valid &&
        s->nes_tri_timer > 0 && s->nes_tri_timer <= 0x7ff) {
        double freq = (s->clock_nes ? s->clock_nes : 1789773u) /
                      (32.0 * (s->nes_tri_timer + 1));
        int block, fnum;
        vgmconv_freq(freq, &block, &fnum);
        if (!s->nes_tri_on) {
            VgmOplSlot mod = {0x01, 0x3F, 0xF0, 0xFF, 0};
            VgmOplSlot car = {0x01, 0x20, 0xE0, 0xFF, 0};
            vgmconv_write_voice(s, o, VGMCONV_NES_BASE + 2, mod, car, 0x30);
            s->nes_tri_on = 1;
        }
        vgmconv_voice_freq(s, o, VGMCONV_NES_BASE + 2, block, fnum, 1);
    } else if (s->nes_tri_on) {
        vgmconv_voice_key(s, o, VGMCONV_NES_BASE + 2, 0);
        s->nes_tri_on = 0;
    }
}

static void vgmconv_nes_update_noise(VgmConverterState *s, VgmConvOutput *o)
{
#ifdef VGMCONV_DISABLE_NES_NOISE
    (void)s; (void)o;
    return;
#else
    if (s->nes_noise_enabled && s->nes_noise_vol > 0) {
        int block, fnum;
        int bd = s->shadow_valid[0xBD] ? s->shadow[0xBD] : 0;
        int idx = s->nes_noise_period & 15;
        int mo, ca, cc, a0, b0;
        int tl = (15 - s->nes_noise_vol) * 3 + 24;
        /* OPL3's SD/HH rhythm units are inherently broadband noise and leave
           a regular hiss around NES drum hits.  Use the pitched TOM unit with
           a low 180..900 Hz range instead: the rhythm remains, but the harsh
           high-frequency component is gone. */
        double f = s->nes_noise_mode ? (300.0 + (15 - idx) * 60.0)
                                     : (180.0 + (15 - idx) * 35.0);
        if (tl > 63) tl = 63;
        vgmconv_freq(f, &block, &fnum);
        vgmconv_emit(s, o, 0xA8, fnum & 0xff);
        vgmconv_emit(s, o, 0xB8, (block << 2) | ((fnum >> 8) & 3));
        /* TOM uses channel 8's modulator operator. */
        vgmconv_voice_regs(8, &mo, &ca, &cc, &a0, &b0);
        vgmconv_emit(s, o, mo, 0x01);       /* MULT=1 */
        vgmconv_emit(s, o, mo + 0x20, tl);  /* volume */
        vgmconv_emit(s, o, mo + 0x40, 0xA0);/* AR=10, DR=0: soft attack */
        vgmconv_emit(s, o, mo + 0x60, 0x0F);/* SL=0, RR=15: quick release */
        vgmconv_emit(s, o, 0xBD, (bd & ~0x1f) | 0x20 | 0x04);
        s->nes_noise_on = 1;
    } else if (s->nes_noise_on) {
        int bd = s->shadow_valid[0xBD] ? s->shadow[0xBD] : 0;
        vgmconv_emit(s, o, 0xBD, bd & ~0x1f);
        s->nes_noise_on = 0;
    }
#endif
}

static void vgmconv_nes_update_all(VgmConverterState *s, VgmConvOutput *o)
{
    vgmconv_nes_update_pulse(s, o, 0);
    vgmconv_nes_update_pulse(s, o, 1);
    vgmconv_nes_update_triangle(s, o);
    vgmconv_nes_update_noise(s, o);
}

static int vgmconv_nes_write(VgmConverterState *s, VgmConvOutput *o,
                             unsigned char addr, unsigned char data)
{
    if (!s->nes_ready) {
        s->nes_ready = 1;
        vgmconv_opl3_init(s, o);
        vgmconv_nes_voice_setup(s, o, VGMCONV_NES_BASE + 0, 0, 0, 0);
        vgmconv_nes_voice_setup(s, o, VGMCONV_NES_BASE + 1, 0, 0, 0);
        vgmconv_nes_voice_setup(s, o, VGMCONV_NES_COPY_BASE + 0, 0, 0, 0);
        vgmconv_nes_voice_setup(s, o, VGMCONV_NES_COPY_BASE + 1, 0, 0, 0);
    }
    switch (addr) {
        case 0x00: case 0x04: {
            int i = addr ? 1 : 0;
            s->nes_pulse_duty[i] = data >> 6;
            s->nes_pulse_vol[i] = data & 0x0f;
            break;
        }
        case 0x02: case 0x03: case 0x06: case 0x07: {
            int i = (addr >> 2) & 1;
            if (addr & 1) {
                s->nes_pulse_timer[i] = (s->nes_pulse_timer[i] & 0xff) | ((data & 7) << 8);
                s->nes_pulse_timer_valid[i] = 1;
            } else {
                s->nes_pulse_timer[i] = (s->nes_pulse_timer[i] & 0x700) | data;
                s->nes_pulse_timer_valid[i] = 1;
            }
            break;
        }
        /* 0x08 is the triangle linear counter, 0x09 is unused.  The timer
           lives in 0x0A (low) and 0x0B (high 3 bits); using 0x08/0x0A here
           made the triangle follow the linear-counter values as pitch. */
        case 0x0A:
            s->nes_tri_timer = (s->nes_tri_timer & 0x700) | data;
            s->nes_tri_timer_valid = 1;
            break;
        case 0x0B:
            s->nes_tri_timer = (s->nes_tri_timer & 0x0ff) | ((data & 7) << 8);
            s->nes_tri_timer_valid = 1;
            break;
        case 0x0C:
            s->nes_noise_vol = data & 0x0f;
            s->nes_noise_mode = (data >> 7) & 1;
            break;
        case 0x0E: s->nes_noise_period = data & 0x0f; break;
        case 0x15:
            s->nes_pulse_enabled[0] = data & 1;
            s->nes_pulse_enabled[1] = (data >> 1) & 1;
            s->nes_tri_enabled = (data >> 2) & 1;
            s->nes_noise_enabled = (data >> 3) & 1;
            break;
        default: return o->n;
    }
    vgmconv_nes_update_all(s, o);
    return o->n;
}

/* ---------------- AY8910 -> OPL3 ---------------- */

static const unsigned char vgmconv_ay_tl[16] = {
    63, 62, 56, 52, 46, 42, 36, 32, 28, 24, 20, 16, 12, 8, 4, 0
};

static void vgmconv_ay_setup_voice(VgmConverterState *s, VgmConvOutput *o,
                                   int voice, int noise)
{
    VgmOplSlot mod, car;
    int c0 = 0xFE;
    if (noise) {
        mod.r20 = 0x0F; car.r20 = 0x0F;
        mod.r40 = 0x04; car.r40 = 0x3F;
    } else {
        mod.r20 = 0x02; car.r20 = 0x04;
        mod.r40 = 0x1A; car.r40 = 0x3F;
    }
    mod.r60 = 0x00; car.r60 = 0xF0;
    mod.r80 = 0x00; car.r80 = 0x00;
    mod.rE0 = 0x00; car.rE0 = 0x00;
    vgmconv_write_voice(s, o, VGMCONV_AY_BASE + voice, mod, car, c0);
}

static void vgmconv_ay_update_tone(VgmConverterState *s, VgmConvOutput *o, int ch)
{
    int tone_on = ((1 << ch) & s->ay_regs[0x07]) == 0;
    int noise_on = ((8 << ch) & s->ay_regs[0x07]) == 0;
    int v = s->ay_regs[0x08 + ch];
    int vol = (v & 0x10) ? 0 : (v & 0x0f);
    int tl = vgmconv_ay_tl[vol & 0x0f];
    int mo, ca, cc, a0, b0;
    vgmconv_voice_regs(VGMCONV_AY_BASE + ch, &mo, &ca, &cc, &a0, &b0);
    vgmconv_emit(s, o, ca + 0x20, tone_on ? tl : 0x3F);
    vgmconv_voice_regs(VGMCONV_AY_BASE + ch + 3, &mo, &ca, &cc, &a0, &b0);
    vgmconv_emit(s, o, ca + 0x20, noise_on ? tl : 0x3F);
}

static void vgmconv_ay_update_noise_freq(VgmConverterState *s, VgmConvOutput *o)
{
    int np = s->ay_regs[0x06] & 0x1f;
    int fnum = (int)(1024.0 / (np + 1.0) - 1.0);
    int ch;
    if (fnum < 0) fnum = 0;
    if (fnum > 1023) fnum = 1023;
    for (ch = 3; ch < 6; ch++)
        vgmconv_voice_freq(s, o, VGMCONV_AY_BASE + ch, 7, fnum, 1);
}

static int vgmconv_ay_write(VgmConverterState *s, VgmConvOutput *o,
                            unsigned char addr, unsigned char data)
{
    int ch;
    s->ay_regs[addr] = data;
    if (!s->ay_ready) {
        s->ay_ready = 1;
        vgmconv_opl3_init(s, o);
        for (ch = 0; ch < 6; ch++) vgmconv_ay_setup_voice(s, o, ch, ch >= 3);
    }
    if (addr <= 0x05) {
        int tp;
        ch = addr >> 1;
        tp = (s->ay_regs[ch * 2 + 1] << 8) | s->ay_regs[ch * 2];
        if (tp > 0) {
            double freq = (s->clock_ay ? s->clock_ay : 1789773u) / (16.0 * tp);
            int block, fnum;
            vgmconv_freq(freq, &block, &fnum);
            vgmconv_voice_freq(s, o, VGMCONV_AY_BASE + ch, block, fnum, 1);
        }
    }
    if (addr >= 0x08 && addr <= 0x0a) vgmconv_ay_update_tone(s, o, addr - 0x08);
    if (addr == 0x06) vgmconv_ay_update_noise_freq(s, o);
    if (addr == 0x07) {
        vgmconv_ay_update_tone(s, o, 0);
        vgmconv_ay_update_tone(s, o, 1);
        vgmconv_ay_update_tone(s, o, 2);
    }
    return o->n;
}

/* ---------------- SN76489 -> OPL3 ---------------- */

static void vgmconv_sn_setup_voice(VgmConverterState *s, VgmConvOutput *o, int voice)
{
    VgmOplSlot mod = {0x02, 0x00, 0xF0, 0xFF, 3};
    VgmOplSlot car = {0x02, 0x00, 0xF0, 0xFF, 3};
    vgmconv_write_voice(s, o, VGMCONV_SN_BASE + voice, mod, car, 0x3E);
}

static void vgmconv_sn_update_tone(VgmConverterState *s, VgmConvOutput *o, int ch)
{
    int div = s->sn_fdiv[ch];
    int vol = s->sn_vol[ch] & 0x0f;
    if (div > 0 && vol < 15) {
        double freq = (s->clock_sn ? s->clock_sn : 3579545u) / (32.0 * div);
        int block, fnum;
        vgmconv_freq(freq, &block, &fnum);
        vgmconv_voice_freq(s, o, VGMCONV_SN_BASE + ch, block, fnum, 1);
        vgmconv_nes_tl(s, o, VGMCONV_SN_BASE + ch, 15 - vol);
    } else {
        vgmconv_voice_key(s, o, VGMCONV_SN_BASE + ch, 0);
    }
}

static void vgmconv_sn_update_noise(VgmConverterState *s, VgmConvOutput *o)
{
    double base = (s->clock_sn ? s->clock_sn : 3579545u);
    int n = s->sn_noise_ctl & 3;
    double freq;
    int vol = s->sn_vol[3] & 0x0f;
    if (n == 3 && s->sn_fdiv[2] > 0)
        freq = base / (32.0 * s->sn_fdiv[2]);
    else
        freq = base / (512.0 * (1 << n));
    if (vol < 15) {
        int block, fnum;
        vgmconv_freq(freq, &block, &fnum);
        vgmconv_voice_freq(s, o, VGMCONV_SN_BASE + 3, block, fnum, 1);
        vgmconv_nes_tl(s, o, VGMCONV_SN_BASE + 3, 15 - vol);
    } else {
        vgmconv_voice_key(s, o, VGMCONV_SN_BASE + 3, 0);
    }
}

static int vgmconv_sn_write(VgmConverterState *s, VgmConvOutput *o,
                            unsigned char data)
{
    int ch, type;
    if (!s->sn_ready) {
        s->sn_ready = 1;
        vgmconv_opl3_init(s, o);
        for (ch = 0; ch < 4; ch++) vgmconv_sn_setup_voice(s, o, ch);
        for (ch = 0; ch < 4; ch++) s->sn_vol[ch] = 0x0f;
    }
    if (data & 0x80) {
        s->sn_ch = (data >> 5) & 3;
        s->sn_type = (data >> 4) & 1;
        ch = s->sn_ch;
        type = s->sn_type;
        if (ch == 3 && type == 0) {
            s->sn_noise_ctl = data & 7;
            vgmconv_sn_update_noise(s, o);
        } else if (type) {
            s->sn_vol[ch] = data & 0x0f;
            if (ch == 3) vgmconv_sn_update_noise(s, o);
            else vgmconv_sn_update_tone(s, o, ch);
        } else {
            s->sn_fdiv[ch] = (s->sn_fdiv[ch] & 0x3f0) | (data & 0x0f);
            if (ch == 3) vgmconv_sn_update_noise(s, o);
            else vgmconv_sn_update_tone(s, o, ch);
        }
    } else {
        ch = s->sn_ch;
        type = s->sn_type;
        if (type) {
            s->sn_vol[ch] = data & 0x0f;
            if (ch == 3) vgmconv_sn_update_noise(s, o);
            else vgmconv_sn_update_tone(s, o, ch);
        } else if (ch < 3) {
            s->sn_fdiv[ch] = (s->sn_fdiv[ch] & 0x00f) | ((data & 0x3f) << 4);
            vgmconv_sn_update_tone(s, o, ch);
        }
    }
    return o->n;
}


/* ---------------- YM2413 (OPLL) -> OPL3 ---------------- */

static const unsigned char vgmconv_opll_rom[16][8] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x71,0x61,0x1E,0x17,0xD0,0x78,0x00,0x17},
    {0x13,0x41,0x1A,0x0D,0xD8,0xF7,0x23,0x13},
    {0x13,0x01,0x99,0x00,0xF2,0xD4,0x21,0x23},
    {0x11,0x61,0x0E,0x07,0x8D,0x64,0x70,0x27},
    {0x32,0x21,0x1E,0x06,0xE1,0x76,0x01,0x28},
    {0x31,0x22,0x16,0x05,0xE0,0x71,0x00,0x18},
    {0x21,0x61,0x1D,0x07,0x82,0x81,0x11,0x07},
    {0x33,0x21,0x2D,0x13,0xB0,0x70,0x00,0x07},
    {0x61,0x61,0x1B,0x06,0x64,0x65,0x10,0x17},
    {0x41,0x61,0x0B,0x18,0x85,0xF0,0x81,0x07},
    {0x33,0x01,0x83,0x11,0xEA,0xEF,0x10,0x04},
    {0x17,0xC1,0x24,0x07,0xF8,0xF8,0x22,0x12},
    {0x61,0x50,0x0C,0x05,0xD2,0xF5,0x40,0x42},
    {0x01,0x01,0x55,0x03,0xE4,0x90,0x03,0x02},
    {0x41,0x41,0x89,0x03,0xF1,0xE4,0xC0,0x13}
};

static void vgmconv_opll_update(VgmConverterState *s, VgmConvOutput *o, int ch)
{
    const unsigned char *d;
    unsigned char custom[8];
    int inst = (s->opll_regs[0x30 + ch] >> 4) & 0x0f;
    int volume = s->opll_regs[0x30 + ch] & 0x0f;
    VgmOplSlot mod, car;
    int fb, c0, fnum_hi, block_key;

    if (inst == 0) {
        int i;
        for (i = 0; i < 8; i++) custom[i] = s->opll_regs[i];
        d = custom;
    } else {
        d = vgmconv_opll_rom[inst & 0x0f];
    }

    mod.r20 = d[0];
    car.r20 = d[1];
    mod.r40 = d[2] & 0x3f;
    car.r40 = (volume * 4 > 63) ? 63 : (volume * 4);
    mod.r60 = d[4];
    car.r60 = d[5];
    mod.r80 = d[6];
    car.r80 = d[7];
    mod.rE0 = (d[3] >> 3) & 1;
    car.rE0 = (d[3] >> 4) & 1;
    fb = d[3] & 7;
    c0 = 0xF0 | ((fb & 7) << 1);
    vgmconv_write_voice(s, o, ch, mod, car, c0);

    fnum_hi = s->opll_regs[0x20 + ch];
    block_key = (fnum_hi & 0x1f) << 1;
    vgmconv_emit(s, o, 0xA0 + ch, (s->opll_regs[0x10 + ch] & 0x7f) << 1);
    vgmconv_emit(s, o, 0xB0 + ch, block_key);
}

static int vgmconv_opll_write(VgmConverterState *s, VgmConvOutput *o,
                              unsigned char addr, unsigned char data)
{
    int ch;
    s->opll_regs[addr] = data;
    if (!s->opll_ready) {
        s->opll_ready = 1;
        vgmconv_opl3_init(s, o);
    }
    if (addr == 0x0e) {
        vgmconv_emit(s, o, 0xBD, 0xc0 | (data & 0x3f));
        return o->n;
    }
    if (addr >= 0x10 && addr <= 0x18) {
        ch = addr - 0x10;
        vgmconv_emit(s, o, 0xA0 + ch, (data & 0x7f) << 1);
        vgmconv_emit(s, o, 0xB0 + ch, (s->opll_regs[0x20 + ch] & 0x1f) << 1);
        return o->n;
    }
    if (addr >= 0x20 && addr <= 0x28) {
        ch = addr - 0x20;
        vgmconv_emit(s, o, 0xA0 + ch, (s->opll_regs[0x10 + ch] & 0x7f) << 1);
        vgmconv_emit(s, o, 0xB0 + ch, (data & 0x1f) << 1);
        return o->n;
    }
    if (addr >= 0x30 && addr <= 0x38) {
        ch = addr - 0x30;
        vgmconv_opll_update(s, o, ch);
        return o->n;
    }
    if (addr <= 0x07) {
        for (ch = 0; ch < 9; ch++) vgmconv_opll_update(s, o, ch);
    }
    return o->n;
}


/* ---------------- YM2612 / YM2203 / YM2608 / YM2610 FM -> OPL3 ----------------
 * This follows the OPNVoice.toOPL mapping used by ym-voice/vgm-conv: every
 * four-operator OPN channel is represented by two two-operator OPL3 voices.
 */

static int vgmconv_opn_ar(int a)
{
    if (a == 0) return 0;
    if (a == 31) return 15;
    a = (a * 28) >> 6;
    if (a < 1) a = 1;
    if (a > 15) a = 15;
    return a;
}

static VgmOplSlot vgmconv_opn_slot(VgmConverterState *s, int port, int local,
                                   int op, int key)
{
    unsigned char *r = s->opn_regs[port];
    int ro = (op == 0 ? 0x30 : op == 1 ? 0x34 : op == 2 ? 0x38 : 0x3c) + local;
    int co = (op == 0 ? 0x40 : op == 1 ? 0x44 : op == 2 ? 0x48 : 0x4c) + local;
    int ao = (op == 0 ? 0x60 : op == 1 ? 0x64 : op == 2 ? 0x68 : 0x6c) + local;
    int so = (op == 0 ? 0x80 : op == 1 ? 0x84 : op == 2 ? 0x88 : 0x8c) + local;
    int sro = (op == 0 ? 0x70 : op == 1 ? 0x74 : op == 2 ? 0x78 : 0x7c) + local;
    int do_ = (op == 0 ? 0x90 : op == 1 ? 0x94 : op == 2 ? 0x98 : 0x9c) + local;
    VgmOplSlot v;
    int ml = r[ro] & 15;
    int tl = r[co] & 0x7f;
    int ar = r[ao] & 31;
    int ks = (r[ao] >> 6) & 3;
    int dr = r[so] & 31;
    int am = (r[so] >> 7) & 1;
    int sl = (r[do_] >> 4) & 15;
    int rr = r[do_] & 15;
    int sr = r[sro] & 31;
    if (tl > 63) tl = 63;
    v.r20 = (unsigned char)((am << 7) | ((key ? 0 : 1) << 5) |
                            ((ks >> 1) << 4) | ml);
    v.r40 = (unsigned char)tl;
    v.r60 = (unsigned char)((vgmconv_opn_ar(ar) << 4) | vgmconv_opn_ar(dr));
    v.r80 = (unsigned char)((sl << 4) |
                            (key ? vgmconv_opn_ar(sr) : (rr ? rr : 1)));
    v.rE0 = 0;
    return v;
}

static void vgmconv_opn_update(VgmConverterState *s, VgmConvOutput *o,
                               int ch)
{
    int port = ch / 3;
    int local = ch % 3;
    int key = s->opn_key[ch];
    int alg = s->opn_regs[port][0xb0 + local] & 7;
    int fb = (s->opn_regs[port][0xb0 + local] >> 3) & 7;
    VgmOplSlot m1 = vgmconv_opn_slot(s, port, local, 0, key);
    VgmOplSlot c1 = vgmconv_opn_slot(s, port, local, 1, key);
    VgmOplSlot m2 = vgmconv_opn_slot(s, port, local, 2, key);
    VgmOplSlot c2 = vgmconv_opn_slot(s, port, local, 3, key);
    VgmOplSlot a, b;
    int conA = 0, conB = 0, fbB = 0;
    int fnum_raw, fnum, block;

    switch (alg) {
        case 0:
            a = m1;
            b = c2;
            b.r20 = (b.r20 & 0xf0) | (m2.r20 & 0x0f);
            {
                int tl = (m2.r40 & 0x3f) - 2;
                if (tl < 0) tl = 0;
                tl += (c2.r40 & 0x3f);
                if (tl > 63) tl = 63;
                b.r40 = (unsigned char)((b.r40 & 0xc0) | tl);
            }
            conA = 0; conB = 0;
            break;
        case 1:
        case 3:
            a = m1;
            b = c2;
            b.r20 = (b.r20 & 0xf0) | (c1.r20 & 0x0f);
            {
                int tl = (c1.r40 & 0x3f) - 2;
                if (tl < 0) tl = 0;
                tl += (c2.r40 & 0x3f);
                if (tl > 63) tl = 63;
                b.r40 = (unsigned char)((b.r40 & 0xc0) | tl);
            }
            conA = 0; conB = 0;
            break;
        case 2:
            a = m1; b = c2;
            conA = 0; conB = 0;
            break;
        case 4:
            a = m1; b = c1;
            conA = 0; conB = 0;
            break;
        case 5:
            a = m1; b = m1;
            b.r40 = (unsigned char)((b.r40 & 0xc0) | (c2.r40 & 0x3f));
            conA = 0; conB = 0; fbB = fb;
            break;
        case 6:
            a = m1; b = m2;
            conA = 0; conB = 1;
            break;
        default:
            a = m1; b = m2;
            conA = 1; conB = 1;
            break;
    }

    {
        int v0 = ch * 2;
        int v1 = v0 + 1;
        vgmconv_write_voice(s, o, v0, a, c1, 0x30 | ((fb & 7) << 1) | conA);
        vgmconv_write_voice(s, o, v1, b, c2, 0x30 | ((fbB & 7) << 1) | conB);
        fnum_raw = ((s->opn_regs[port][0xa4 + local] & 7) << 8) |
                   s->opn_regs[port][0xa0 + local];
        fnum = fnum_raw >> 1;
        block = (s->opn_regs[port][0xa4 + local] >> 3) & 7;
        vgmconv_voice_freq(s, o, v0, block, fnum, key);
        vgmconv_voice_freq(s, o, v1, block, fnum, key);
    }
}

static int vgmconv_opn_write(VgmConverterState *s, VgmConvOutput *o,
                             int port, unsigned char addr, unsigned char data)
{
    int ch;
    s->opn_regs[port][addr] = data;
    if (!s->opn_ready) {
        s->opn_ready = 1;
        vgmconv_opl3_init(s, o);
    }
    if (addr == 0x28) {
        int nch = data & 3;
        int chh = port ? (3 + nch) : (nch + ((data & 4) ? 3 : 0));
        if (chh < 6) {
            s->opn_key[chh] = (data & 0xf0) != 0;
            vgmconv_opn_update(s, o, chh);
        }
        return o->n;
    }
    if (addr >= 0x30 && addr <= 0x9f) {
        int local = addr & 3;
        if (local < 3) {
            ch = port * 3 + local;
            vgmconv_opn_update(s, o, ch);
        }
        return o->n;
    }
    if ((addr >= 0xa0 && addr <= 0xa2) || (addr >= 0xa4 && addr <= 0xa6) ||
        (addr >= 0xb0 && addr <= 0xb2)) {
        int local = addr & 3;
        if (local < 3) {
            ch = port * 3 + local;
            vgmconv_opn_update(s, o, ch);
        }
    }
    return o->n;
}

#endif /* VGMCONVERT_H */

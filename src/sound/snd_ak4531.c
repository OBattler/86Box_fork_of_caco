/*
 * 86Box     A hypervisor and IBM PC system emulator that specializes in
 *           running old operating systems and software designed for IBM
 *           PC systems and compatibles from 1981 through fairly recent
 *           system designs based on the PCI bus.
 *
 *           This file is part of the 86Box distribution.
 *
 *           Asahi Kasei AK4531A codec emulation, used in ES1370.
 * 
 * Authors:  Cacodemon345.
 * 
 * Copyright 2023 Cacodemon345.
 *
 */

#include <stdint.h>
#include <stdlib.h>
#include <memory.h>
#include <stdio.h>
#include <math.h>

#include <86box/86box.h>

typedef struct ak4531a_t
{
    uint8_t regs[256];
    int aux_gain[32];
    int aux_attn[32];
} ak4531a_t;

void ak4531a_write_reg(void* priv, uint8_t reg, uint8_t val)
{
    ak4531a_t* codec = (ak4531a_t*)priv;

    codec->regs[reg] = val;
}

void ak4351a_reset(ak4531a_t* codec)
{
    int i = 0;
    for (i = 0; i < 32; i++)
    {
        codec->aux_attn[i] = (int)(pow(10., (i * -2.0) / 10.0) * 65536);
        codec->aux_gain[i] = (int)(pow(10., (12. - (i * -2.0)) / 10.0) * 65536);
    }

    codec->regs[0] = codec->regs[1] = 0x80;
    for (i = 0x2; i < 0xF; i++)
    {
        codec->regs[i] = 0x86;
    }
    codec->regs[0xF] = 0x80;

    codec->regs[0x10] = codec->regs[0x11] = codec->regs[0x12] = codec->regs[0x13] = 
    codec->regs[0x14] = codec->regs[0x15] = 0;
}

/* ES1370 Master */
void ak4351a_attn_master(void* priv, int* master_l, int* master_r)
{
    ak4531a_t* codec = (ak4531a_t*)priv;

    *master_l = (codec->regs[0x00] & 0x80) ? 0 : codec->aux_gain[codec->regs[0x00] & 0x1f];
    *master_r = (codec->regs[0x01] & 0x80) ? 0 : codec->aux_gain[codec->regs[0x01] & 0x1f];
}

/* ES1370 Wave */
void ak4351a_attn_dac1(void* priv, int* dac1_l, int* dac1_r)
{
    ak4531a_t* codec = (ak4531a_t*)priv;

    *dac1_l = (codec->regs[0x02] & 0x80) ? 0 : codec->aux_attn[codec->regs[0x02] & 0x1f];
    *dac1_r = (codec->regs[0x03] & 0x80) ? 0 : codec->aux_attn[codec->regs[0x03] & 0x1f];
}

/* ES1370 Synth */
void ak4351a_attn_dac2(void* priv, int* dac2_l, int* dac2_r)
{
    ak4531a_t* codec = (ak4531a_t*)priv;

    *dac2_l = (codec->regs[0x04] & 0x80) ? 0 : codec->aux_attn[codec->regs[0x04] & 0x1f];
    *dac2_r = (codec->regs[0x05] & 0x80) ? 0 : codec->aux_attn[codec->regs[0x05] & 0x1f];
}

void* ak4531a_create(void)
{
    void* codec = calloc(1, sizeof(ak4531a_t));
    if (codec) {
        ak4351a_reset((ak4531a_t*)codec);
    }
    else fatal("Out of memory!\n");
    return codec;
}
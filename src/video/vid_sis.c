/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          SiS 6202 emulation.
 *
 *
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *          Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2008-2018 Sarah Walker.
 *          Copyright 2016-2018 Miran Grca.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include <stdatomic.h>
#include <86box/86box.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/device.h>
#include <86box/timer.h>
#include <86box/video.h>
#include <86box/vid_svga.h>
#include <86box/vid_svga_render.h>
#include <86box/pci.h>
#include <86box/thread.h>
#include <assert.h>

#pragma pack(push, 1)
typedef struct sis_accel_t {
    uint32_t src_start_address;
    uint32_t dst_start_address;

    uint16_t src_pitch;
    uint16_t dst_pitch;

    uint16_t rect_width;
    uint16_t rect_height;

    uint32_t fg_color_rop;
    uint32_t bg_color_rop;

    uint16_t mask[4];

    uint16_t left_clip;
    uint16_t right_clip;
    uint16_t top_clip;
    uint16_t bottom_clip;

    uint32_t cmd_status;
    uint8_t  pattern[64];
} sis_accel_t;

typedef struct sis_accel_line_t {
    uint32_t x_start;
    uint32_t y_start;

    uint16_t src_pitch; /* Reserved */
    uint16_t dst_pitch; /* Reserved */

    int16_t  line_length;
    uint16_t reserved_2;

    uint32_t fg_color_rop;
    uint32_t bg_color_rop;

    int16_t  k1, k2;
    int16_t  error;
    uint16_t line_style;

    uint16_t left_clip;
    uint16_t right_clip;
    uint16_t top_clip;
    uint16_t bottom_clip;

    uint32_t cmd_status;
    uint8_t  pattern[64];
} sis_accel_line_t;
#pragma(pack, pop)

typedef struct sis_cpu_bitblt_fifo_t {
    uint32_t dst;
    uint32_t len;
    uint32_t dat, orig_dat;
} sis_cpu_bitblt_fifo_t;

typedef struct sis_t {
    svga_t        svga;
    uint8_t       pci_conf_status;
    uint8_t       pci_line_interrupt;
    uint8_t       pci_rom_enable;
    uint8_t       write_bank, read_bank;
    atomic_bool   engine_active;
    atomic_bool   quit;
    thread_t     *accel_thread;
    event_t      *fifo_event, *fifo_data_event;
    uint16_t      rom_addr;
    mem_mapping_t linear_mapping, mmio_mapping;

    union {
        uint8_t          accel_regs[128];
        uint16_t         accel_regs_w[64];
        uint32_t         accel_regs_l[32];
        sis_accel_t      accel;
        sis_accel_line_t accel_line;
    };

    union {
        sis_accel_t      accel_cur;
        sis_accel_line_t accel_line_cur;
    };
    union {
        sis_accel_t      accel_queue[32];
        sis_accel_line_t accel_line_queue[32];
    };

    atomic_int accel_fifo_read_idx;
    atomic_int accel_fifo_write_idx;

    /* CPU-driven BitBlt members. */
    int cpu_pixel_count;

    rom_t bios_rom;
    uint8_t slot;
} sis_t;

#define FIFO_EMPTY(sis)   (sis->accel_fifo_write_idx == sis->accel_fifo_read_idx)
#define FIFO_ENTRIES(sis) (sis->accel_fifo_write_idx - sis->accel_fifo_read_idx)

static video_timings_t timing_sis = { .type = VIDEO_PCI, .write_b = 2, .write_w = 2, .write_l = 4, .read_b = 20, .read_w = 20, .read_l = 35 };

static void
sis_render_24bpp_highres(svga_t *svga)
{
    int       x;
    uint32_t *p;
    uint32_t  dat;
    uint32_t  changed_addr;
    uint32_t  addr;

    if ((svga->displine + svga->y_add) < 0)
        return;

    {
        changed_addr = svga->remap_func(svga, svga->ma);

        if (svga->changedvram[changed_addr >> 12] || svga->changedvram[(changed_addr >> 12) + 1] || svga->fullchange) {
            p = &svga->monitor->target_buffer->line[svga->displine + svga->y_add][svga->x_add];

            if (svga->firstline_draw == 2000)
                svga->firstline_draw = svga->displine;
            svga->lastline_draw = svga->displine;

            if (!svga->remap_required) {
                for (x = 0; x <= (svga->hdisp + svga->scrollcache); x++) {
                    dat  = *(uint32_t *) (&svga->vram[(svga->ma + (x * 3)) & svga->vram_display_mask]);
                    *p++ = (dat & 0xffffff);
                }
                svga->ma += (x * 3);
            } else {
                for (x = 0; x <= (svga->hdisp + svga->scrollcache); x++) {
                    addr = svga->remap_func(svga, svga->ma);
                    dat  = *(uint32_t *) (&svga->vram[addr & svga->vram_display_mask]);
                    *p++ = (dat & 0xffffff);

                    svga->ma += 3;
                }
            }
            svga->ma &= svga->vram_display_mask;
        }
    }
}

void
sis_recalctimings(svga_t *svga)
{
    sis_t *sis = (sis_t *) svga->priv;

    svga->ma_latch |= (svga->seqregs[0x27] & 0xF) << 16;
    svga->vblankstart |= !!(svga->seqregs[0x0A] & 0x4) << 10;
    svga->vsyncstart += !!(svga->seqregs[0x0A] & 0x8) << 10;
    svga->vtotal += !!(svga->seqregs[0x0A] & 0x1) << 10;
    svga->dispend += !!(svga->seqregs[0x0A] & 0x2) << 10;
    svga->rowoffset |= (svga->seqregs[0x0A] & 0xF0) << 4;
    if (svga->seqregs[0x6] & 0x2) {
        svga->interlace = (svga->seqregs[0x6] & 0x20);
        if (svga->seqregs[0x6] & 0x4) {
            svga->bpp    = 15;
            svga->render = svga_render_15bpp_highres;
        } else if (svga->seqregs[0x6] & 0x8) {
            svga->bpp    = 16;
            svga->render = svga_render_16bpp_highres;
        } else if (svga->seqregs[0x6] & 0x10) {
            svga->bpp    = 24;
            svga->render = sis_render_24bpp_highres;
            // pclog("SiS: 16.7M\n");
        }
        if (svga->hdisp == 1280 || svga->hdisp == 1024)
            svga->rowoffset >>= 1;
    }
    // pclog("SiS: hdisp = %d, dispend = %d, enhanced = %d\n", svga->hdisp, svga->dispend, !!(svga->seqregs[0x6] & 0x2));
}

void
sis_recalc_banking(sis_t *sis)
{
    sis->svga.read_bank  = 0;
    sis->svga.write_bank = 0;
    if (sis->svga.seqregs[0x6] & 0x2) {
        if (sis->svga.seqregs[0xb] & 0x8) {
            sis->svga.read_bank  = 65536 * (sis->read_bank & 0x3f);
            sis->svga.write_bank = 65536 * (sis->write_bank & 0x3f);
        } else {
            sis->svga.read_bank  = 65536 * ((sis->write_bank & 0xf0) >> 4);
            sis->svga.write_bank = 65536 * (sis->write_bank & 0xf);
        }
    }
}

#define SWAP(a, b) \
    tmpswap = a;   \
    a       = b;   \
    b       = tmpswap;

void
sis_do_rop_8bpp(uint8_t *dst, uint8_t src, uint8_t rop)
{
    switch (rop) {
        case 0x00:
            *dst = 0;
            break;
        case 0x11:
            *dst = ~(*dst) & ~src;
            break;
        case 0x22:
            *dst &= ~src;
            break;
        case 0x33:
            *dst = ~src;
            break;
        case 0x44:
            *dst = src & ~(*dst);
            break;
        case 0x55:
            *dst = ~*dst;
            break;
        case 0x66:
            *dst ^= src;
            break;
        case 0x77:
            *dst = ~src | ~(*dst);
            break;
        case 0x88:
            *dst &= src;
            break;
        case 0x99:
            *dst ^= ~src;
            break;
        case 0xAA:
            break; /* No-op. */
        case 0xBB:
            *dst |= ~src;
            break;
        case 0xCC:
            *dst = src;
            break;
        case 0xDD:
            *dst = src | ~(*dst);
            break;
        case 0xEE:
            *dst |= src;
            break;
        case 0xFF:
            *dst = 0xFF;
            break;
    }
}

void
sis_do_rop_15bpp(uint16_t *dst, uint16_t src, uint8_t rop)
{
    uint16_t orig_dst = *dst & 0x8000;
    switch (rop) {
        case 0x00:
            *dst = 0;
            break;
        case 0x11:
            *dst = ~(*dst) & ~src;
            break;
        case 0x22:
            *dst &= ~src;
            break;
        case 0x33:
            *dst = ~src;
            break;
        case 0x44:
            *dst = src & ~(*dst);
            break;
        case 0x55:
            *dst = ~*dst;
            break;
        case 0x66:
            *dst ^= src;
            break;
        case 0x77:
            *dst = ~src | ~(*dst);
            break;
        case 0x88:
            *dst &= src;
            break;
        case 0x99:
            *dst ^= ~src;
            break;
        case 0xAA:
            break; /* No-op. */
        case 0xBB:
            *dst |= ~src;
            break;
        case 0xCC:
            *dst = src;
            break;
        case 0xDD:
            *dst = src | ~(*dst);
            break;
        case 0xEE:
            *dst |= src;
            break;
        case 0xFF:
            *dst = ~0;
            break;
    }
    *dst &= 0x7FFF;
    *dst |= orig_dst;
}

void
sis_do_rop_16bpp(uint16_t *dst, uint16_t src, uint8_t rop)
{
    switch (rop) {
        case 0x00:
            *dst = 0;
            break;
        case 0x11:
            *dst = ~(*dst) & ~src;
            break;
        case 0x22:
            *dst &= ~src;
            break;
        case 0x33:
            *dst = ~src;
            break;
        case 0x44:
            *dst = src & ~(*dst);
            break;
        case 0x55:
            *dst = ~*dst;
            break;
        case 0x66:
            *dst ^= src;
            break;
        case 0x77:
            *dst = ~src | ~(*dst);
            break;
        case 0x88:
            *dst &= src;
            break;
        case 0x99:
            *dst ^= ~src;
            break;
        case 0xAA:
            break; /* No-op. */
        case 0xBB:
            *dst |= ~src;
            break;
        case 0xCC:
            *dst = src;
            break;
        case 0xDD:
            *dst = src | ~(*dst);
            break;
        case 0xEE:
            *dst |= src;
            break;
        case 0xFF:
            *dst = 0xFF;
            break;
    }
}

void
sis_do_rop_24bpp(uint32_t *dst, uint32_t src, uint8_t rop)
{
    uint32_t orig_dst = *dst & 0xFF000000;
    switch (rop) {
        case 0x00:
            *dst = 0;
            break;
        case 0x11:
            *dst = ~(*dst) & ~src;
            break;
        case 0x22:
            *dst &= ~src;
            break;
        case 0x33:
            *dst = ~src;
            break;
        case 0x44:
            *dst = src & ~(*dst);
            break;
        case 0x55:
            *dst = ~*dst;
            break;
        case 0x66:
            *dst ^= src;
            break;
        case 0x77:
            *dst = ~src | ~(*dst);
            break;
        case 0x88:
            *dst &= src;
            break;
        case 0x99:
            *dst ^= ~src;
            break;
        case 0xAA:
            break; /* No-op. */
        case 0xBB:
            *dst |= ~src;
            break;
        case 0xCC:
            *dst = src;
            break;
        case 0xDD:
            *dst = src | ~(*dst);
            break;
        case 0xEE:
            *dst |= src;
            break;
        case 0xFF:
            *dst = 0xFF;
            break;
    }
    *dst &= 0xFFFFFF;
    *dst |= orig_dst;
}

void
sis_do_rop_8bpp_patterned(uint8_t *dst, uint8_t src, uint8_t nonpattern_src, uint8_t rop)
{
    switch (rop) {
        case 0x00:
            *dst = 0;
            break;
        case 0x05:
            *dst = ~(*dst) & ~src;
            break;
        case 0x0A:
            *dst &= ~src;
            break;
        case 0x0F:
            *dst = ~src;
            break;
        case 0x50:
            *dst = src & ~(*dst);
            break;
        case 0x55:
            *dst = ~*dst;
            break;
        case 0x5A:
            *dst ^= src;
            break;
        case 0x5F:
            *dst = ~src | ~(*dst);
            break;
        case 0xB8:
            *dst = (((src ^ *dst) & nonpattern_src) ^ src);
            break;
        case 0xA0:
            *dst &= src;
            break;
        case 0xA5:
            *dst ^= ~src;
            break;
        case 0xAA:
            break; /* No-op. */
        case 0xAF:
            *dst |= ~src;
            break;
        case 0xF0:
            *dst = src;
            break;
        case 0xF5:
            *dst = src | ~(*dst);
            break;
        case 0xFA:
            *dst |= src;
            break;
        case 0xFF:
            *dst = 0xFF;
            break;
    }
}

void
sis_do_rop_15bpp_patterned(uint16_t *dst, uint16_t src, uint8_t nonpattern_src, uint8_t rop)
{
    uint16_t orig_dst = *dst & 0x8000;
    switch (rop) {
        case 0x00:
            *dst = 0;
            break;
        case 0x05:
            *dst = ~(*dst) & ~src;
            break;
        case 0x0A:
            *dst &= ~src;
            break;
        case 0x0F:
            *dst = ~src;
            break;
        case 0x50:
            *dst = src & ~(*dst);
            break;
        case 0x55:
            *dst = ~*dst;
            break;
        case 0x5A:
            *dst ^= src;
            break;
        case 0x5F:
            *dst = ~src | ~(*dst);
            break;
        case 0xB8:
            *dst = (((src ^ *dst) & nonpattern_src) ^ src);
            break;
        case 0xA0:
            *dst &= src;
            break;
        case 0xA5:
            *dst ^= ~src;
            break;
        case 0xAA:
            break; /* No-op. */
        case 0xAF:
            *dst |= ~src;
            break;
        case 0xF0:
            *dst = src;
            break;
        case 0xF5:
            *dst = src | ~(*dst);
            break;
        case 0xFA:
            *dst |= src;
            break;
        case 0xFF:
            *dst = 0xFF;
            break;
    }
    *dst &= 0x7FFF;
    *dst |= orig_dst;
}

void
sis_do_rop_16bpp_patterned(uint16_t *dst, uint16_t src, uint8_t nonpattern_src, uint8_t rop)
{
    switch (rop) {
        case 0x00:
            *dst = 0;
            break;
        case 0x05:
            *dst = ~(*dst) & ~src;
            break;
        case 0x0A:
            *dst &= ~src;
            break;
        case 0x0F:
            *dst = ~src;
            break;
        case 0x50:
            *dst = src & ~(*dst);
            break;
        case 0x55:
            *dst = ~*dst;
            break;
        case 0x5A:
            *dst ^= src;
            break;
        case 0x5F:
            *dst = ~src | ~(*dst);
            break;
        case 0xB8:
            *dst = (((src ^ *dst) & nonpattern_src) ^ src);
            break;
        case 0xA0:
            *dst &= src;
            break;
        case 0xA5:
            *dst ^= ~src;
            break;
        case 0xAA:
            break; /* No-op. */
        case 0xAF:
            *dst |= ~src;
            break;
        case 0xF0:
            *dst = src;
            break;
        case 0xF5:
            *dst = src | ~(*dst);
            break;
        case 0xFA:
            *dst |= src;
            break;
        case 0xFF:
            *dst = 0xFF;
            break;
    }
}

void
sis_do_rop_24bpp_patterned(uint32_t *dst, uint32_t src, uint8_t nonpattern_src, uint8_t rop)
{
    uint32_t orig_dst = *dst & 0xFF000000;
    switch (rop) {
        case 0x00:
            *dst = 0;
            break;
        case 0x05:
            *dst = ~(*dst) & ~src;
            break;
        case 0x0A:
            *dst &= ~src;
            break;
        case 0x0F:
            *dst = ~src;
            break;
        case 0x50:
            *dst = src & ~(*dst);
            break;
        case 0x55:
            *dst = ~*dst;
            break;
        case 0x5A:
            *dst ^= src;
            break;
        case 0x5F:
            *dst = ~src | ~(*dst);
            break;
        case 0xB8:
            *dst = (((src ^ *dst) & nonpattern_src) ^ src);
            break;
        case 0xA0:
            *dst &= src;
            break;
        case 0xA5:
            *dst ^= ~src;
            break;
        case 0xAA:
            break; /* No-op. */
        case 0xAF:
            *dst |= ~src;
            break;
        case 0xF0:
            *dst = src;
            break;
        case 0xF5:
            *dst = src | ~(*dst);
            break;
        case 0xFA:
            *dst |= src;
            break;
        case 0xFF:
            *dst = 0xFF;
            break;
    }
    *dst &= 0xFFFFFF;
    *dst |= orig_dst;
}

void
sis_expand_color_font(sis_t *sis)
{
    int      xdir;
    int      ydir;
    int      dx                    = 0;
    int      dy                    = 0;
    uint16_t width                 = sis->accel_cur.rect_width + 1;
    uint16_t height                = sis->accel_cur.rect_height + 1;
    int32_t  dest_addr             = sis->accel_cur.dst_start_address;
    uint32_t src_addr              = sis->accel_cur.src_start_address;
    uint32_t src_addr_pattern_vram = sis->accel_cur.src_start_address;
    uint16_t orig_width            = width;

    xdir = sis->accel_cur.cmd_status & (0x10 << 16) ? 1 : -1;
    ydir = sis->accel_cur.cmd_status & (0x20 << 16) ? 1 : -1;
    pclog("bg_color_rop = 0x%08X, fg_color_rop = 0x%08X, cmd_status = 0x%08X, src_pitch = %d, dst_pitch = %d, width = %d, height = %d (color/font expand)\n", sis->accel_cur.bg_color_rop, sis->accel_cur.fg_color_rop, sis->accel_cur.cmd_status, sis->accel_cur.src_pitch, sis->accel_cur.dst_pitch, width, height);

    while (height) {
        width = orig_width;
        dx    = 0;
        while (width) {
            switch (sis->svga.bpp) {
                case 15:
                case 16:
                    {
                        uint16_t *dst = (uint16_t *) &sis->svga.vram[dest_addr + (dy * sis->accel_cur.dst_pitch * 2) + (dx * 2)];
                        /* TODO: Check this later. */
                        break;
                    }
                case 8:{
                    uint8_t dst         = svga_readb_linear(dest_addr + (dy * sis->accel_cur.dst_pitch) + (dx), &sis->svga);
                    uint8_t *src_pattern = ((sis->accel_cur.cmd_status >> 24) & 0x20) ? (uint8_t *) &sis->svga.vram[src_addr_pattern_vram + (dy * sis->accel_cur.src_pitch) + (dx)] : sis->accel_cur.pattern;
                    uint8_t  src         = 0;
                    uint8_t  srcrop      = 0;
                    uint8_t  pat         = 0;
                    uint8_t  patrop      = 0;
                    switch ((sis->accel_cur.cmd_status >> 18) & 0x3) {
                        case 0x0:
                            pat    = sis->accel_cur.bg_color_rop & 0xFF;
                            patrop = (sis->accel_cur.bg_color_rop >> 24);
                            break;
                        case 0x1:
                            pat    = sis->accel_cur.fg_color_rop & 0xFF;
                            patrop = (sis->accel_cur.fg_color_rop >> 24);
                            break;
                        case 0x2:
                            pat = *(uint32_t *) (&sis->accel_cur.pattern[(dy & 0xF) * 4]);
                            break;
                    }
                    switch ((sis->accel_cur.cmd_status >> 16) & 0x3) {
                        case 0x0:
                            src    = sis->accel_cur.bg_color_rop & 0xFF;
                            srcrop = sis->accel_cur.bg_color_rop >> 24;
                            break;
                        case 0x1:
                            src    = sis->accel_cur.fg_color_rop & 0xFF;
                            srcrop = sis->accel_cur.fg_color_rop >> 24;
                            break;
                        case 0x2:
                            srcrop = 0xCC;
                            src    = svga_readb_linear(src_addr + (dy * sis->accel_cur.src_pitch) + (dx), &sis->svga);
                            break;
                    }

                    if (src_pattern[(dy * sis->accel_cur.src_pitch) + (dx / 8)] & 1 << (dx & 7)) {
                        if ((patrop & 0xF) != (patrop >> 4))
                            sis_do_rop_8bpp_patterned(&dst, pat, src, patrop);
                        else
                            sis_do_rop_8bpp(&dst, pat, patrop);
                    } else {
                        
                        //sis_do_rop_8bpp_patterned(&dst, (((sis->accel_cur.cmd_status >> 16) & 0x3) == 0x0) ? (sis->accel_cur.fg_color_rop & 0xFF) : (sis->accel_cur.bg_color_rop & 0xFF), src, pat >> 24);
                    }
                    svga_writeb_linear(dest_addr + (dy * sis->accel_cur.dst_pitch) + (dx), dst, &sis->svga);
                    break;}
            }
            dx += xdir;
            width--;
        }
        dy += ydir;
        height--;
    }
}

void
sis_draw_line(sis_t *sis)
{
    int xdir;
    int ydir;
    int y = sis->accel_line_cur.line_length;
    int x = 0;
    int dmajor;
    int err;
    int steep = 1;
    int dminor;
    int dx = sis->accel_line_cur.x_start;
    int dy = sis->accel_line_cur.y_start;
    int destxtmp;
    int tmpswap;
    int src_dat;
    int logical_width = 1024 << ((sis->svga.seqregs[0x27] >> 4) & 3);

    if (sis->svga.bpp >= 24)
        return;

    if (sis->svga.bpp == 15 || sis->svga.bpp == 16)
        logical_width >>= 1;

    dminor   = sis->accel_line_cur.k1 >> 1;
    destxtmp = sis->accel_line_cur.k2;
    dmajor   = -(destxtmp - (dminor << 1)) >> 1;

    err = sis->accel_line_cur.error;

    xdir = sis->accel_line_cur.cmd_status & (0x10 << 16) ? 1 : -1;
    ydir = sis->accel_line_cur.cmd_status & (0x20 << 16) ? 1 : -1;

    if (!(sis->accel_line_cur.cmd_status & (0x4 << 16))) {
        steep = 0;
        SWAP(dx, dy);
        SWAP(xdir, ydir);
    }

    while (y >= 0) {
        if (sis->accel_line_cur.cmd_status & (0x0040 << 16)) {
            if (sis->accel_line_cur.cmd_status & (0x0080 << 16) && (dx >= sis->accel_line_cur.left_clip && dy >= sis->accel_line_cur.top_clip && dx <= sis->accel_line_cur.right_clip && dy <= sis->accel_line_cur.bottom_clip)) {
                goto advance;
            }
            if (!(sis->accel_line_cur.cmd_status & (0x0080 << 16)) && (dx < sis->accel_line_cur.left_clip && dy < sis->accel_line_cur.top_clip && dx > sis->accel_line_cur.right_clip && dy > sis->accel_line_cur.bottom_clip)) {
                goto advance;
            }
        }
        switch (sis->svga.bpp) {
            case 16:
            case 15:
                {
                    // uint16_t *dst = (uint16_t *) &sis->svga.vram[(dy * logical_width * 2) + (dx * 2)];
                    uint16_t dst = svga_readw_linear((dy * logical_width * 2) + (dx * 2), &sis->svga);
                    uint16_t src = sis->accel_line_cur.line_style & (1 << (y & 15)) ? (sis->accel_line_cur.fg_color_rop & 0xFFFF) : (sis->accel_line_cur.bg_color_rop & 0xFFFF);
                    if (sis->svga.bpp == 16)
                        sis_do_rop_16bpp(&dst, src, sis->accel_line_cur.line_style & (1 << (y & 15)) ? (sis->accel_line_cur.fg_color_rop >> 24) : (sis->accel_line_cur.bg_color_rop >> 24));
                    else
                        sis_do_rop_15bpp(&dst, src, sis->accel_line_cur.line_style & (1 << (y & 15)) ? (sis->accel_line_cur.fg_color_rop >> 24) : (sis->accel_line_cur.bg_color_rop >> 24));
                    svga_writew_linear((dy * logical_width * 2) + (dx * 2), dst, &sis->svga);
                    break;
                }
            case 8:
                {
                    uint8_t dst = svga_readb_linear((dy * logical_width) + (dx), &sis->svga);
                    uint8_t src = sis->accel_line_cur.line_style & (1 << (y & 15)) ? (sis->accel_line_cur.fg_color_rop & 0xFF) : (sis->accel_line_cur.bg_color_rop & 0xFF);
                    sis_do_rop_8bpp(&dst, src, sis->accel_line_cur.line_style & (1 << (y & 15)) ? (sis->accel_line_cur.fg_color_rop >> 24) : (sis->accel_line_cur.bg_color_rop >> 24));
                    svga_writeb_linear((dy * logical_width) + (dx), dst, &sis->svga);
                    break;
                }
        }

advance:
        if (!y) {
            break;
        }

        while (err > 0) {
            dy += ydir;
            err -= (dmajor << 1);
        }

        dx += xdir;
        err += (dminor << 1);

        x++;
        y--;
    }
}

void
sis_bitblt(sis_t *sis)
{
    int      xdir;
    int      ydir;
    int      dx         = 0;
    int      dy         = 0;
    uint16_t width      = sis->accel_cur.rect_width + 1;
    uint16_t height     = sis->accel_cur.rect_height + 1;
    int32_t  dest_addr  = sis->accel_cur.dst_start_address;
    uint32_t src_addr   = sis->accel_cur.src_start_address;
    uint16_t orig_width = width;
    pclog("bg_color_rop = 0x%08X, fg_color_rop = 0x%08X, cmd_status = 0x%08X, src_pitch = %d, dst_pitch = %d, width = %d, height = %d (BitBlt, discarded)\n", sis->accel_cur.bg_color_rop, sis->accel_cur.fg_color_rop, sis->accel_cur.cmd_status, sis->accel_cur.src_pitch, sis->accel_cur.dst_pitch, width, height);

    xdir = sis->accel_cur.cmd_status & (0x10 << 16) ? 1 : -1;
    ydir = sis->accel_cur.cmd_status & (0x20 << 16) ? 1 : -1;

    while (height) {
        width = orig_width;
        dx    = 0;
        while (width) {
            switch (sis->svga.bpp) {
                case 8:{
                    uint8_t  src            = 0;
                    uint8_t  srcrop         = 0;
                    uint8_t  pat            = 0;
                    uint8_t  patrop         = 0;
                    uint32_t real_dest_addr = dest_addr + (dy * sis->accel_cur.dst_pitch) + dx;
                    uint8_t  dst_dat        = svga_readb_linear(dest_addr + (dy * sis->accel_cur.dst_pitch) + dx, &sis->svga);
                    // uint8_t* dst = &sis->svga.vram[dest_addr + (dy * sis->accel_cur.dst_pitch) + dx];
                    switch ((sis->accel_cur.cmd_status >> 16) & 3) {
                        case 0:
                            src    = sis->accel_cur.bg_color_rop & 0xFF;
                            srcrop = sis->accel_cur.bg_color_rop >> 24;
                            break;
                        case 1:
                            src    = sis->accel_cur.fg_color_rop & 0xFF;
                            srcrop = sis->accel_cur.fg_color_rop >> 24;
                            break;
                        case 2:
                            src    = svga_readb_linear(src_addr + (dy * sis->accel_cur.src_pitch) + dx, &sis->svga);
                            srcrop = 0xCC;
                            break;
                    }
                    switch ((sis->accel_cur.cmd_status >> 18) & 3) {
                        case 0:
                            pat    = sis->accel_cur.bg_color_rop & 0xFF;
                            patrop = sis->accel_cur.bg_color_rop >> 24;
                            break;
                        case 1:
                            pat    = sis->accel_cur.fg_color_rop & 0xFF;
                            patrop = sis->accel_cur.fg_color_rop >> 24;
                            break;
                        case 2:
                            pat    = sis->accel_cur.pattern[4 * (dy & 7) + (dx & 3)];
                            patrop = 0xCC;
                            break;
                    }
                    if ((patrop & 0xF) != (patrop >> 4)) {
                        sis_do_rop_8bpp_patterned(&dst_dat, pat, src, patrop);
                    } else {
                        sis_do_rop_8bpp(&dst_dat, src, patrop);
                    }
                    svga_writeb_linear(real_dest_addr, dst_dat, &sis->svga);
                    // if (srcrop ==)
                    // if (src)
                    break;}
            }
            width--;
            dx += xdir;
        }
        height--;
        dy += ydir;
    }
}

void
sis_cpu_bitblt(sis_t *sis, uint32_t dst, uint32_t src)
{
    int      xdir;
    int      ydir;
    int cpu_pixel_count = sis->cpu_pixel_count;

    if (cpu_pixel_count && sis->svga.bpp == 8) {
        uint8_t pat     = 0;
        uint8_t patrop  = 0;
        uint8_t dst_dat = svga_readb_linear(dst, &sis->svga);
        switch ((sis->accel_cur.cmd_status >> 18) & 3) {
            case 0:
                pat    = sis->accel_cur.bg_color_rop & 0xFF;
                patrop = sis->accel_cur.bg_color_rop >> 24;
                break;
            case 1:
                pat    = sis->accel_cur.fg_color_rop & 0xFF;
                patrop = sis->accel_cur.fg_color_rop >> 24;
                break;
            case 2:
                //pat    = sis->accel_cur.pattern[4 * (dy & 7) + (dx & 3)];
                //patrop = 0xCC;
                break;
        }
        if ((patrop & 0xF) != (patrop >> 4)) {
            sis_do_rop_8bpp_patterned(&dst_dat, pat, src, patrop);
        } else {
            sis_do_rop_8bpp(&dst_dat, src, patrop);
        }
        svga_writeb_linear(dst_dat, src, &sis->svga);
        cpu_pixel_count--;
        if (cpu_pixel_count == 0)
            sis->engine_active = 0;
    }
}

void
sis_accel_thread(void *p)
{
    sis_t *sis = (sis_t *) p;

    while (!sis->quit) {
        if (FIFO_ENTRIES(sis)) {
            sis->accel_cur           = sis->accel_queue[sis->accel_fifo_read_idx];
            sis->accel_fifo_read_idx = (sis->accel_fifo_read_idx + 1) & 0x1f;
            sis->engine_active       = 1;
            switch ((sis->accel_cur.cmd_status >> 24) & 3) {
                case 0:
                    sis_bitblt(sis);
                    break;
                case 3:
                    sis_draw_line(sis);
                    break;
                case 2:
                    sis_expand_color_font(sis);
                    break;
                default:
                    pclog("bg_color_rop = 0x%08X, fg_color_rop = 0x%08X, cmd_status = 0x%08X, src_pitch = %d, dst_pitch = %d (discarded)\n", sis->accel_cur.bg_color_rop, sis->accel_cur.fg_color_rop, sis->accel_cur.cmd_status, sis->accel_cur.src_pitch, sis->accel_cur.dst_pitch);
                    break;
            }
        }
        if (!FIFO_ENTRIES(sis)) {
            sis->engine_active = 0;
            thread_wait_event(sis->fifo_event, -1);
            thread_reset_event(sis->fifo_event);
        }
    }
}

void
sis_out(uint16_t addr, uint8_t val, void *p)
{
    sis_t  *sis  = (sis_t *) p;
    svga_t *svga = &sis->svga;
    uint8_t old;

    if (((addr & 0xfff0) == 0x3d0 || (addr & 0xfff0) == 0x3b0) && !(svga->miscout & 1))
        addr ^= 0x60;

    // if (!(addr == 0x3CC || addr == 0x3C9)) pclog("SiS SVGA out: 0x%X, 0x%X\n", addr, val);
    switch (addr) {
        case 0x3CB:
            sis->read_bank = val & 0x3f;
            sis_recalc_banking(sis);
            break;
        case 0x3CD:
            sis->write_bank = val;
            sis_recalc_banking(sis);
            break;
        case 0x3C5:
            if (svga->seqaddr >= 0x6) {
                if (svga->seqaddr == 0xD || svga->seqaddr == 0xE)
                    return;
                if (svga->seqregs[0x05] == 0x86) {
                    svga->seqregs[svga->seqaddr] = val;
                    switch (svga->seqaddr) {
                        case 0x20:
                        case 0x21:
                            mem_mapping_set_addr(&sis->linear_mapping, (svga->seqregs[0x20] << 19) | ((svga->seqregs[0x21] & 0x1F) << 27), 1 << (19 + (svga->seqregs[0x21] >> 5)));
                            if (!(svga->seqregs[0x6] & 0x80)) {
                                mem_mapping_disable(&sis->linear_mapping);
                            }
                            break;
                        case 0x27:
                        case 0x6:
                        case 0xA:
                            if (svga->seqaddr == 0x6) {
                                mem_mapping_disable(&sis->linear_mapping);
                                if (val & 0x80)
                                    mem_mapping_enable(&sis->linear_mapping);
                                sis_recalc_banking(sis);
                            }
                            svga_recalctimings(svga);
                            break;
                        case 0xB:
                            if (sis->svga.seqregs[0xb] & 0x60) {
                                switch ((sis->svga.seqregs[0xb] & 0x60) >> 5) {
                                    case 0x00:
                                        {
                                            mem_mapping_disable(&sis->mmio_mapping);
                                            break;
                                        }
                                    case 0x01:
                                        {
                                            mem_mapping_set_addr(&sis->mmio_mapping, 0xA8000, 0x1000);
                                            break;
                                        }
                                    case 0x02:
                                    case 0x03:
                                        {
                                            mem_mapping_set_addr(&sis->mmio_mapping, 0xB8000, 0x1000);
                                            break;
                                        }
                                }
                            }
                            sis_recalc_banking(sis);
                            break;
                    }
                }
                return;
            }
            break;
        case 0x3D4:
            svga->crtcreg = val & 0x3f;
            return;
        case 0x3D5:
            if (svga->crtcreg & 0x20)
                return;
            if ((svga->crtcreg < 7) && (svga->crtc[0x11] & 0x80))
                return;
            if ((svga->crtcreg == 7) && (svga->crtc[0x11] & 0x80))
                val = (svga->crtc[7] & ~0x10) | (val & 0x10);
            old                       = svga->crtc[svga->crtcreg];
            svga->crtc[svga->crtcreg] = val;
            if (old != val) {
                if (svga->crtcreg < 0xe || svga->crtcreg > 0x10) {
                    if ((svga->crtcreg == 0xc) || (svga->crtcreg == 0xd)) {
                        svga->fullchange = 3;
                        svga->ma_latch   = ((svga->crtc[0xc] << 8) | svga->crtc[0xd]) + ((svga->crtc[8] & 0x60) >> 5);
                    } else {
                        svga->fullchange = changeframecount;
                        svga_recalctimings(svga);
                    }
                }
            }
            break;
    }
    svga_out(addr, val, svga);
}

uint8_t
sis_in(uint16_t addr, void *p)
{
    sis_t  *sis  = (sis_t *) p;
    svga_t *svga = &sis->svga;
    uint8_t temp;

    if (((addr & 0xfff0) == 0x3d0 || (addr & 0xfff0) == 0x3b0) && !(svga->miscout & 1))
        addr ^= 0x60;

    // if (!(addr == 0x3CC || addr == 0x3C9)) pclog("SiS SVGA in: 0x%X\n", addr);
    switch (addr) {
        case 0x3CB:
            return sis->read_bank & 0x3f;
        case 0x3CD:
            return sis->write_bank;
        case 0x3C5:
            if (svga->seqaddr == 0x05) {
                return (svga->seqregs[0x05] == 0x86) ? 0xA1 : 0x21;
            }
            return svga->seqregs[svga->seqaddr];
        case 0x3D4:
            temp = svga->crtcreg;
            break;
        case 0x3D5:
            if (svga->crtcreg & 0x20)
                temp = 0xff;
            else
                temp = svga->crtc[svga->crtcreg];
            break;
        default:
            temp = svga_in(addr, svga);
            break;
    }
    return temp;
}

static uint8_t
sis_pci_read(int func, int addr, void *p)
{
    sis_t *sis = (sis_t *) p;

    {
        switch (addr) {
            case 0x00:
            case 0x01:
                return (0x1039 >> ((addr & 1) * 8)) & 0xFF;
            case 0x02:
            case 0x03:
                return (0x0002 >> ((addr & 1) * 8)) & 0xFF;
            case 0x04:
                return sis->pci_conf_status;
            case 0x07:
                return 0x02;
            case 0x08:
            case 0x09:
            case 0x0a:
                return 0x00;
            case 0x0b:
                return 0x03;
            case 0x30:
                return sis->pci_rom_enable & 0x1;
            case 0x31:
                return 0x0;
            case 0x32:
                return sis->rom_addr & 0xFF;
            case 0x33:
                return (sis->rom_addr & 0xFF00) >> 8;
            case 0x3c:
                return sis->pci_line_interrupt;
            case 0x3d:
                return 0x01;
            default:
                return 0x00;
        }
    }
}

static void
sis_pci_write(int func, int addr, uint8_t val, void *p)
{
    sis_t *sis = (sis_t *) p;

    {
        switch (addr) {
            case 0x04:
                {
                    sis->pci_conf_status = val;
                    io_removehandler(0x03c0, 0x0020, sis_in, NULL, NULL, sis_out, NULL, NULL, sis);
                    mem_mapping_disable(&sis->bios_rom.mapping);
                    mem_mapping_disable(&sis->linear_mapping);
                    mem_mapping_disable(&sis->svga.mapping);
                    mem_mapping_disable(&sis->mmio_mapping);
                    if (sis->pci_conf_status & PCI_COMMAND_IO) {
                        io_sethandler(0x03c0, 0x0020, sis_in, NULL, NULL, sis_out, NULL, NULL, sis);
                    }
                    if (sis->pci_conf_status & PCI_COMMAND_MEM) {
                        mem_mapping_enable(&sis->bios_rom.mapping);
                        mem_mapping_enable(&sis->svga.mapping);
                        if (sis->svga.seqregs[0x06] & 0x80)
                            mem_mapping_enable(&sis->linear_mapping);
                        if (sis->svga.seqregs[0xb] & 0x60) {
                            switch ((sis->svga.seqregs[0xb] & 0x60) >> 5) {
                                case 0x00:
                                    {
                                        mem_mapping_disable(&sis->mmio_mapping);
                                        break;
                                    }
                                case 0x01:
                                    {
                                        mem_mapping_set_addr(&sis->mmio_mapping, 0xA8000, 0x1000);
                                        break;
                                    }
                                case 0x02:
                                case 0x03:
                                    {
                                        mem_mapping_set_addr(&sis->mmio_mapping, 0xB8000, 0x1000);
                                        break;
                                    }
                            }
                        }
                    }
                    break;
                }
            case 0x3c:
                sis->pci_line_interrupt = val;
                break;
            case 0x30:
                sis->pci_rom_enable = val & 0x1;
                mem_mapping_disable(&sis->bios_rom.mapping);
                if (sis->pci_rom_enable & 1) {
                    mem_mapping_set_addr(&sis->bios_rom.mapping, sis->rom_addr << 16, 0x8000);
                }
                break;
            case 0x32:
                sis->rom_addr &= ~0xFF;
                sis->rom_addr |= val;
                if (sis->pci_rom_enable & 1) {
                    mem_mapping_set_addr(&sis->bios_rom.mapping, sis->rom_addr << 16, 0x8000);
                }
                break;
            case 0x33:
                sis->rom_addr &= ~0xFF00;
                sis->rom_addr |= (val << 8);
                if (sis->pci_rom_enable & 1) {
                    mem_mapping_set_addr(&sis->bios_rom.mapping, sis->rom_addr << 16, 0x8000);
                }
                break;
        }
    }
}

uint8_t
sis_mmio_read(uint32_t addr, void *p)
{
    sis_t  *sis = (sis_t *) p;
    uint8_t val = 0xFF;
    if ((addr & 0xFFFF) >= 0x8280) {
        val = sis->accel_regs[((addr & 0xFFFF) - 0x8280)];
        if ((addr & 0xFFFF) == 0x82AB) {
            val &= 0xD0;
            if (FIFO_EMPTY(sis))
                val |= 0x80;
            if (sis->engine_active)
                val |= 0x40;
        }
    }
    return val;
}

uint16_t
sis_mmio_readw(uint32_t addr, void *p)
{
    sis_t   *sis = (sis_t *) p;
    uint16_t val = 0xFFFF;
    if ((addr & 0xFFFF) >= 0x8280) {
        val = sis->accel_regs_w[((addr & 0xFFFF) - 0x8280) / 2];
        if ((addr & 0xFFFF) == 0x82AA) {
            val &= 0xD000;
            if (FIFO_EMPTY(sis))
                val |= 0x8000;
            if (sis->engine_active)
                val |= 0x4000;
        }
    }
    return val;
}

uint32_t
sis_mmio_readl(uint32_t addr, void *p)
{
    sis_t   *sis = (sis_t *) p;
    uint32_t val = 0xFFFFFFFF;
    if ((addr & 0xFFFF) >= 0x8280) {
        val = sis->accel_regs_l[((addr & 0xFFFF) - 0x8280) / 4];
        if ((addr & 0xFFFF) == 0x82A8) {
            val &= 0xD0000000;
            if (FIFO_EMPTY(sis))
                val |= 0x80000000;
            if (sis->engine_active)
                val |= 0x40000000;
        }
    }
    return val;
}

void
sis_mmio_write(uint32_t addr, uint8_t val, void *p)
{
    sis_t *sis = (sis_t *) p;
    if ((addr & 0xFFFF) >= 0x8280) {
        sis->accel_regs[((addr & 0xFFFF) - 0x8280)] = val;
        if ((addr & 0xFFFF) == 0x82AB) {
            if (((sis->accel.cmd_status >> 16) & 3) == 3) {
                /*while (sis->engine_active) {
                }
                sis->engine_active = 1;
                sis->accel_cur     = sis->accel;
                sis->cpu_pixel_count = (sis->accel_cur.rect_width + 1) * (sis->accel_cur.rect_height + 1);*/
                return;
            }
            sis->engine_active                          = 1;
            sis->accel_queue[sis->accel_fifo_write_idx] = sis->accel;
            sis->accel_fifo_write_idx                   = (sis->accel_fifo_write_idx + 1) & 0x1f;
            thread_set_event(sis->fifo_event);
        }
    }
}

void
sis_mmio_writew(uint32_t addr, uint16_t val, void *p)
{
    sis_t *sis = (sis_t *) p;
    if ((addr & 0xFFFF) >= 0x8280) {
        sis->accel_regs_w[((addr & 0xFFFF) - 0x8280) / 2] = val;
        if ((addr & 0xFFFF) == 0x82AA) {
            if (((sis->accel.cmd_status >> 16) & 3) == 3) {
                /*while (sis->engine_active) {
                }
                sis->engine_active = 1;
                sis->accel_cur     = sis->accel;
                sis->cpu_pixel_count = (sis->accel_cur.rect_width + 1) * (sis->accel_cur.rect_height + 1);*/
                return;
            }
            sis->engine_active                          = 1;
            sis->accel_queue[sis->accel_fifo_write_idx] = sis->accel;
            sis->accel_fifo_write_idx                   = (sis->accel_fifo_write_idx + 1) & 0x1f;
            thread_set_event(sis->fifo_event);
        }
    }
}

void
sis_mmio_writel(uint32_t addr, uint32_t val, void *p)
{
    sis_t *sis = (sis_t *) p;
    if ((addr & 0xFFFF) >= 0x8280) {
        sis->accel_regs_l[((addr & 0xFFFF) - 0x8280) / 4] = val;
        if ((addr & 0xFFFF) == 0x82AA || (addr & 0xFFFF) == 0x82A8) {
            if (((sis->accel.cmd_status >> 16) & 3) == 3) {
                /* FIXME: Freezes */
                /*while (sis->engine_active) {
                }
                sis->engine_active = 1;
                sis->accel_cur     = sis->accel;
                sis->cpu_pixel_count = (sis->accel_cur.rect_width + 1) * (sis->accel_cur.rect_height + 1);*/
                return;
            }
            sis->engine_active                          = 1;
            sis->accel_queue[sis->accel_fifo_write_idx] = sis->accel;
            sis->accel_fifo_write_idx                   = (sis->accel_fifo_write_idx + 1) & 0x1f;
            thread_set_event(sis->fifo_event);
        }
    }
}

uint8_t
sis_read(uint32_t addr, void *p)
{
    sis_t *sis = (sis_t *) p;
    if (sis->svga.seqregs[0xb] & 0x60) {
        switch ((sis->svga.seqregs[0xb] & 0x60) >> 5) {
            case 0x01:
                {
                    if ((addr >> 16) == 0xA && (addr & 0xFFFF) >= 0x8280) {
                        return sis_mmio_read(addr, p);
                    }
                }
            case 0x02:
            case 0x03:
                {
                    if ((addr >> 16) == 0xB && (addr & 0xFFFF) >= 0x8280) {
                        return sis_mmio_read(addr, p);
                    }
                }
        }
    }
    return svga_read(addr, p);
}

uint16_t
sis_readw(uint32_t addr, void *p)
{
    sis_t *sis = (sis_t *) p;
    if (sis->svga.seqregs[0xb] & 0x60) {
        switch ((sis->svga.seqregs[0xb] & 0x60) >> 5) {
            case 0x01:
                {
                    if ((addr >> 16) == 0xA && (addr & 0xFFFF) >= 0x8280) {
                        return sis_mmio_readw(addr, p);
                    }
                }
            case 0x02:
            case 0x03:
                {
                    if ((addr >> 16) == 0xB && (addr & 0xFFFF) >= 0x8280) {
                        return sis_mmio_readw(addr, p);
                    }
                }
        }
    }
    return svga_readw(addr, p);
}

uint32_t
sis_readl(uint32_t addr, void *p)
{
    sis_t *sis = (sis_t *) p;
    if (sis->svga.seqregs[0xb] & 0x60) {
        switch ((sis->svga.seqregs[0xb] & 0x60) >> 5) {
            case 0x01:
                {
                    if ((addr >> 16) == 0xA && (addr & 0xFFFF) >= 0x8280) {
                        return sis_mmio_readl(addr, p);
                    }
                }
            case 0x02:
            case 0x03:
                {
                    if ((addr >> 16) == 0xB && (addr & 0xFFFF) >= 0x8280) {
                        return sis_mmio_readl(addr, p);
                    }
                }
        }
    }
    return svga_readl(addr, p);
}

void
sis_write(uint32_t addr, uint8_t val, void *p)
{
    sis_t *sis = (sis_t *) p;
    if (sis->svga.seqregs[0xb] & 0x60) {
        switch ((sis->svga.seqregs[0xb] & 0x60) >> 5) {
            case 0x01:
                {
                    if ((addr >> 16) == 0xA && (addr & 0xFFFF) >= 0x8280) {
                        sis_mmio_write(addr, val, p);
                        return;
                    }
                }
            case 0x02:
            case 0x03:
                {
                    if ((addr >> 16) == 0xB && (addr & 0xFFFF) >= 0x8280) {
                        sis_mmio_write(addr, val, p);
                        return;
                    }
                }
        }
    }
    svga_write(addr, val, p);
}

void
sis_writew(uint32_t addr, uint16_t val, void *p)
{
    sis_t *sis = (sis_t *) p;
    if (sis->svga.seqregs[0xb] & 0x60) {
        switch ((sis->svga.seqregs[0xb] & 0x60) >> 5) {
            case 0x01:
                {
                    if ((addr >> 16) == 0xA && (addr & 0xFFFF) >= 0x8280) {
                        sis_mmio_writew(addr, val, p);
                        return;
                    }
                }
            case 0x02:
            case 0x03:
                {
                    if ((addr >> 16) == 0xB && (addr & 0xFFFF) >= 0x8280) {
                        sis_mmio_writew(addr, val, p);
                        return;
                    }
                }
        }
    }
    svga_writew(addr, val, p);
}

void
sis_writel(uint32_t addr, uint32_t val, void *p)
{
    sis_t *sis = (sis_t *) p;
    if (sis->svga.seqregs[0xb] & 0x60) {
        switch ((sis->svga.seqregs[0xb] & 0x60) >> 5) {
            case 0x01:
                {
                    if ((addr >> 16) == 0xA && (addr & 0xFFFF) >= 0x8280) {
                        sis_mmio_writel(addr, val, p);
                        return;
                    }
                }
            case 0x02:
            case 0x03:
                {
                    if ((addr >> 16) == 0xB && (addr & 0xFFFF) >= 0x8280) {
                        sis_mmio_writel(addr, val, p);
                        return;
                    }
                }
        }
    }
    svga_writel(addr, val, p);
}

void
sis_writeb_linear(uint32_t addr, uint8_t val, void *p)
{
    svga_t *svga = (svga_t *) p;
    sis_t  *sis  = (sis_t *) svga->priv;

    if (sis->engine_active && (sis->svga.seqregs[0xb] & 0x1)) {
        sis_cpu_bitblt(sis, addr, val);
    } else
        svga_writeb_linear(addr, val, p);
}

void
sis_writew_linear(uint32_t addr, uint16_t val, void *p)
{
    svga_t *svga = (svga_t *) p;
    sis_t  *sis  = (sis_t *) svga->priv;

    if (sis->engine_active && (sis->svga.seqregs[0xb] & 0x1)) {
        if (sis->svga.bpp == 8) {
            sis_cpu_bitblt(sis, addr, val);
            sis_cpu_bitblt(sis, addr + 1, val >> 8);
        } else {
            sis_cpu_bitblt(sis, addr, val);
        }
    } else
        svga_writew_linear(addr, val, p);
}

void
sis_writel_linear(uint32_t addr, uint32_t val, void *p)
{
    svga_t *svga = (svga_t *) p;
    sis_t  *sis  = (sis_t *) svga->priv;

    if (sis->engine_active && (sis->svga.seqregs[0xb] & 0x1)) {
        if (sis->svga.bpp == 8) {
            sis_cpu_bitblt(sis, addr, val);
            sis_cpu_bitblt(sis, addr + 1, val >> 8);
            sis_cpu_bitblt(sis, addr + 2, val >> 16);
            sis_cpu_bitblt(sis, addr + 3, val >> 24);
        } else {
            sis_cpu_bitblt(sis, addr, val);
        }
    } else
        svga_writel_linear(addr, val, p);
}

static void *
sis_init(const device_t *info)
{
    sis_t *sis = malloc(sizeof(sis_t));
    memset(sis, 0, sizeof(sis_t));

    rom_init(&sis->bios_rom, "roms/video/sis/BIOS.BIN", 0xc0000, 0x8000, 0x7fff, 0x0000, MEM_MAPPING_EXTERNAL);
    mem_mapping_disable(&sis->bios_rom.mapping);

    video_inform(VIDEO_FLAG_TYPE_SPECIAL, &timing_sis);

    svga_init(info, &sis->svga, sis, 1 << 22, /*4096kb*/
              NULL,
              sis_in, sis_out,
              NULL,
              NULL);

    io_sethandler(0x03c0, 0x0020, sis_in, NULL, NULL, sis_out, NULL, NULL, sis);

    pci_add_card(PCI_ADD_VIDEO, sis_pci_read, sis_pci_write, sis, &sis->slot);

    sis->svga.bpp              = 8;
    sis->svga.miscout          = 1;
    sis->svga.recalctimings_ex = sis_recalctimings;

    mem_mapping_add(&sis->linear_mapping, 0, 0, svga_readb_linear, svga_readw_linear, svga_readl_linear, sis_writeb_linear, sis_writew_linear, sis_writel_linear, NULL, MEM_MAPPING_EXTERNAL, sis);
    mem_mapping_add(&sis->mmio_mapping, 0, 0, sis_mmio_read, sis_mmio_readw, sis_mmio_readl, sis_mmio_write, sis_mmio_writew, sis_mmio_writel, NULL, MEM_MAPPING_EXTERNAL, sis);
    mem_mapping_set_handler(&sis->svga.mapping, sis_read, sis_readw, sis_readl, sis_write, sis_writew, sis_writel);

    sis->quit            = 0;
    sis->engine_active   = 0;
    sis->fifo_event      = thread_create_event();
    sis->fifo_data_event = thread_create_event();
    sis->accel_thread    = thread_create(sis_accel_thread, (void *) sis);

    return sis;
}

static int
sis_available(void)
{
    return rom_present("roms/video/sis/BIOS.BIN");
}

void
sis_close(void *p)
{
    sis_t *sis = (sis_t *) p;

    sis->quit = 1;
    thread_set_event(sis->fifo_event);
    thread_wait(sis->accel_thread);
    svga_close(&sis->svga);

    free(sis);
}

void
sis_speed_changed(void *p)
{
    sis_t *sis = (sis_t *) p;

    svga_recalctimings(&sis->svga);
}

void
sis_force_redraw(void *p)
{
    sis_t *sis = (sis_t *) p;

    sis->svga.fullchange = changeframecount;
}

const device_t sis_6202_device = {
    .name          = "SiS 6202",
    .internal_name = "sis_6202",
    .flags         = DEVICE_PCI,
    .local         = 0,
    .init          = sis_init,
    .close         = sis_close,
    .reset         = NULL,
    { .available = sis_available },
    .speed_changed = sis_speed_changed,
    .force_redraw  = sis_force_redraw,
    .config        = NULL
};

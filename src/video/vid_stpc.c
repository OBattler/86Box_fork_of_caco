/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          IBM VGA emulation.
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
#include <86box/86box.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/device.h>
#include <86box/timer.h>
#include <86box/video.h>
#include <86box/vid_svga.h>
#include <86box/vid_svga_render.h>
typedef struct stpc_vid_t {
    svga_t svga;
} stpc_vid_t;

static video_timings_t timing_vga = { VIDEO_ISA, 8, 16, 32, 8, 16, 32 };

void    stpc_vid_out(uint16_t addr, uint8_t val, void *p);
uint8_t stpc_vid_in(uint16_t addr, void *p);

static video_timings_t timing_ps1_stpc_vid_isa = { .type = VIDEO_ISA, .write_b = 6, .write_w = 8, .write_l = 16, .read_b = 6, .read_w = 8, .read_l = 16 };

static void
stpc_vid_recalctimings(svga_t* svga)
{
    svga->ma_latch  |= ((svga->crtc[0x19] & 0xF) << 16);
    svga->rowoffset |= ((svga->crtc[0x19] & 0x30) << 4);

    svga->hblank_end_val |= (svga->crtc[0x25] & 0x10) << 2;
    svga->vblankstart    |= (!!(svga->crtc[0x25] & 0x8)) << 10;
    svga->vsyncstart     |= (!!(svga->crtc[0x25] & 0x4)) << 10;
    svga->dispend        |= (!!(svga->crtc[0x25] & 0x2)) << 10;
    svga->vtotal         |= (!!(svga->crtc[0x25] & 0x1)) << 10;

    if (svga->crtc[0x28] & 0x7) {
        switch (svga->crtc[0x28] & 0x7) {
            case 1:
                svga->bpp = 8;
                svga->render = svga_render_8bpp_highres;
                break;
            case 2:
                svga->bpp = 15;
                svga->render = svga_render_15bpp_highres;
                break;
            case 3:
                svga->bpp = 16;
                svga->render = svga_render_16bpp_highres;
                break;
            case 4:
                svga->bpp = 24;
                svga->render = svga_render_24bpp_highres;
                break;
            case 5:
                svga->bpp = 32;
                svga->render = svga_render_32bpp_highres;
                break;
        }
    }
}

void
stpc_vid_out(uint16_t addr, uint8_t val, void *p)
{
    stpc_vid_t *vga  = (stpc_vid_t *) p;
    svga_t     *svga = &vga->svga;
    uint8_t     old;

    if (((addr & 0xfff0) == 0x3d0 || (addr & 0xfff0) == 0x3b0) && !(svga->miscout & 1))
        addr ^= 0x60;
    switch (addr) {
        case 0x3D4:
            svga->crtcreg = val & 0x3f;
            return;
        case 0x3D5:
            if (svga->crtcreg >= 0x19) {
                if (svga->seqregs[0x6] == 0x57) {
                    old                       = svga->crtc[svga->crtcreg];
                    svga->crtc[svga->crtcreg] = val;

                    if (old != val) {
                        switch (svga->crtcreg) {
                            case 0x19:
                                svga_recalctimings(svga);
                                break;
                            case 0x25:
                                svga_recalctimings(svga);
                                break;
                            case 0x28:
                                svga_recalctimings(svga);
                                break;
                        }
                    }
                }
                else return;
            }
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

uint32_t
stpc_vid_get_addr(uint32_t addr, void *p)
{
    svga_t  *svga = (svga_t *) p;

    if (svga->crtc[0x1C] & 0x1) {
        if (svga->chain4 && svga->packed_chain4) {
            addr += (svga->gdcreg[0x6] & 4) ? (1 << 15) : (1 << 16);
        } else {
            addr += (svga->gdcreg[0x6] & 4) ? (1 << 17) : (1 << 18);
        }
    }
    return addr;
}

uint8_t
stpc_vid_in(uint16_t addr, void *p)
{
    stpc_vid_t  *vga  = (stpc_vid_t *) p;
    svga_t *svga = &vga->svga;
    uint8_t temp;

    if (((addr & 0xfff0) == 0x3d0 || (addr & 0xfff0) == 0x3b0) && !(svga->miscout & 1))
        addr ^= 0x60;

    switch (addr) {
        case 0x3C5:
            {
                if (svga->seqaddr == 0x6) {
                    return (svga->seqregs[0x6] == 0x57) ? 1 : 0;
                }
                break;
            }
        case 0x3D4:
            temp = svga->crtcreg;
            break;
        case 0x3D5:
            if (svga->crtcreg >= 0x20 && svga->seqregs[0x6] != 0x57)
                temp = 0xff;
            else {
                switch (svga->crtcreg) {
                    case 0x3A:
                        temp = 0x01;
                        break;
                    case 0x3B:
                        temp = 0x01;
                        break;
                    default:
                        temp = svga->crtc[svga->crtcreg];
                        break;
                }
            }
            break;
        default:
            temp = svga_in(addr, svga);
            break;
    }
    return temp;
}

void *
stpc_vid_init(const device_t *info)
{
    stpc_vid_t *stpc_vid = malloc(sizeof(stpc_vid_t));
    memset(stpc_vid, 0, sizeof(stpc_vid_t));

    video_inform(VIDEO_FLAG_TYPE_SPECIAL, &timing_ps1_stpc_vid_isa);

    svga_init(info, &stpc_vid->svga, stpc_vid, 1 << 22, /*4096kb*/
              stpc_vid_recalctimings,
              stpc_vid_in, stpc_vid_out,
              NULL,
              NULL);

    io_sethandler(0x03c0, 0x0020, stpc_vid_in, NULL, NULL, stpc_vid_out, NULL, NULL, stpc_vid);

    stpc_vid->svga.bpp     = 8;
    stpc_vid->svga.miscout = 1;

    pclog("STPC video init\n");
    return stpc_vid;
}

static int
stpc_vid_available(void)
{
    return 1;
}

void
stpc_vid_close(void *p)
{
    stpc_vid_t *stpc_vid = (stpc_vid_t *) p;

    svga_close(&stpc_vid->svga);

    free(stpc_vid);
}

void
stpc_vid_speed_changed(void *p)
{
    stpc_vid_t *stpc_vid = (stpc_vid_t *) p;

    svga_recalctimings(&stpc_vid->svga);
}

void
stpc_vid_force_redraw(void *p)
{
    stpc_vid_t *stpc_vid = (stpc_vid_t *) p;

    stpc_vid->svga.fullchange = changeframecount;
}

const device_t stpc_vid_device = {
    .name          = "STPC integrated video",
    .internal_name = "stpc_vid",
    .flags         = DEVICE_ISA,
    .local         = 0,
    .init          = stpc_vid_init,
    .close         = stpc_vid_close,
    .reset         = NULL,
    { .available = stpc_vid_available },
    .speed_changed = stpc_vid_speed_changed,
    .force_redraw  = stpc_vid_force_redraw,
    .config        = NULL
};

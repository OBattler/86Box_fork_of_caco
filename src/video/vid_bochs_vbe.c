/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Bochs VBE SVGA emulation.
 *
 *          Uses code from libxcvt to calculate CRTC timings.
 *
 * Authors: Cacodemon345
 *          The Bochs Project
 *          Fabrice Bellard
 *          The libxcvt authors
 *
 *          Copyright 2024 Cacodemon345
 *          Copyright 2003 Fabrice Bellard
 *          Copyright 2002-2024 The Bochs Project
 *          Copyright 2002-2003 Mike Nordell
 *          Copyright 2000-2021 The libxcvt authors
 *
 *          See https://gitlab.freedesktop.org/xorg/lib/libxcvt/-/blob/master/COPYING for libxcvt license details
 */

#include <stdbool.h>
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
#include <86box/pci.h>
#include <86box/i2c.h>
#include <86box/vid_ddc.h>

#define VBE_DISPI_BANK_SIZE_KB           64
#define VBE_DISPI_BANK_GRANULARITY_KB    32

#define VBE_DISPI_MAX_XRES               1920
#define VBE_DISPI_MAX_YRES               1600

#define VBE_DISPI_IOPORT_INDEX           0x01CE
#define VBE_DISPI_IOPORT_DATA            0x01CF

#define VBE_DISPI_INDEX_ID               0x0
#define VBE_DISPI_INDEX_XRES             0x1
#define VBE_DISPI_INDEX_YRES             0x2
#define VBE_DISPI_INDEX_BPP              0x3
#define VBE_DISPI_INDEX_ENABLE           0x4
#define VBE_DISPI_INDEX_BANK             0x5
#define VBE_DISPI_INDEX_VIRT_WIDTH       0x6
#define VBE_DISPI_INDEX_VIRT_HEIGHT      0x7
#define VBE_DISPI_INDEX_X_OFFSET         0x8
#define VBE_DISPI_INDEX_Y_OFFSET         0x9
#define VBE_DISPI_INDEX_VIDEO_MEMORY_64K 0xa
#define VBE_DISPI_INDEX_DDC              0xb

#define VBE_DISPI_ID0                    0xB0C0
#define VBE_DISPI_ID1                    0xB0C1
#define VBE_DISPI_ID2                    0xB0C2
#define VBE_DISPI_ID3                    0xB0C3
#define VBE_DISPI_ID4                    0xB0C4
#define VBE_DISPI_ID5                    0xB0C5

#define VBE_DISPI_DISABLED               0x00
#define VBE_DISPI_ENABLED                0x01
#define VBE_DISPI_GETCAPS                0x02
#define VBE_DISPI_BANK_GRANULARITY_32K   0x10
#define VBE_DISPI_8BIT_DAC               0x20
#define VBE_DISPI_LFB_ENABLED            0x40
#define VBE_DISPI_NOCLEARMEM             0x80

#define VBE_DISPI_BANK_WR                0x4000
#define VBE_DISPI_BANK_RD                0x8000
#define VBE_DISPI_BANK_RW                0xc000


static video_timings_t timing_bochs  = { .type = VIDEO_PCI, .write_b = 2, .write_w = 2, .write_l = 1, .read_b = 20, .read_w = 20, .read_l = 21 };

struct vbe_mode_info {
    uint32_t                hdisplay;
    uint32_t                vdisplay;
    float                   vrefresh;
    float                   hsync;
    uint64_t                dot_clock;
    uint16_t                hsync_start;
    uint16_t                hsync_end;
    uint16_t                htotal;
    uint16_t                vsync_start;
    uint16_t                vsync_end;
    uint16_t                vtotal;
};

typedef struct bochs_vbe_t {
    svga_t svga;

    rom_t bios_rom;
    uint16_t vbe_regs[16];
    uint16_t vbe_index;

    uint16_t bank_gran;
    mem_mapping_t linear_mapping;

    uint8_t pci_conf_status, pci_regs[256];
    uint8_t pci_rom_enable;
    uint8_t pci_line_interrupt;
    uint8_t slot;
    uint16_t rom_addr;

    void *i2c;
    void *ddc;
} bochs_vbe_t;

static void
gen_mode_info(int hdisplay, int vdisplay, float vrefresh, struct vbe_mode_info* mode_info)
{
    bool margins = false;
    float vfield_rate, hperiod;
    int hdisplay_rnd, hmargin;
    int vdisplay_rnd, vmargin, vsync;
    float interlace;            /* Please rename this */

    if (!mode_info)
        return;

    mode_info->hdisplay = hdisplay;
    mode_info->vdisplay = vdisplay;
    mode_info->vrefresh = vrefresh;

    /* 1) top/bottom margin size (% of height) - default: 1.8 */
#define CVT_MARGIN_PERCENTAGE 1.8

    /* 2) character cell horizontal granularity (pixels) - default 8 */
#define CVT_H_GRANULARITY 8

    /* 4) Minimum vertical front porch (lines) - default 3 */
#define CVT_MIN_V_PORCH_RND 3

    /* 4) Minimum number of vertical back porch lines - default 6 */
#define CVT_MIN_V_BPORCH 6

    /* Pixel Clock step (kHz) */
#define CVT_CLOCK_STEP 250

    /* CVT default is 60.0Hz */
    if (!mode_info->vrefresh)
        mode_info->vrefresh = 60.0;

    /* 1. Required field rate */
    vfield_rate = mode_info->vrefresh;

    /* 2. Horizontal pixels */
    hdisplay_rnd = mode_info->hdisplay - (mode_info->hdisplay % CVT_H_GRANULARITY);

    /* 3. Determine left and right borders */
    if (margins) {
        /* right margin is actually exactly the same as left */
        hmargin = (((float) hdisplay_rnd) * CVT_MARGIN_PERCENTAGE / 100.0);
        hmargin -= hmargin % CVT_H_GRANULARITY;
    }
    else {
        hmargin = 0;
    }

    /* 4. Find total active pixels */
    mode_info->hdisplay = hdisplay_rnd + 2 * hmargin;

    /* 5. Find number of lines per field */
    vdisplay_rnd = mode_info->vdisplay;

    /* 6. Find top and bottom margins */
    /* nope. */
    if (margins)
        /* top and bottom margins are equal again. */
        vmargin = (((float) vdisplay_rnd) * CVT_MARGIN_PERCENTAGE / 100.0);
    else
        vmargin = 0;

    mode_info->vdisplay = mode_info->vdisplay + 2 * vmargin;

    /* 7. interlace */
    interlace = 0.0;

    /* Determine vsync Width from aspect ratio */
    if (!(mode_info->vdisplay % 3) && ((mode_info->vdisplay * 4 / 3) == mode_info->hdisplay))
        vsync = 4;
    else if (!(mode_info->vdisplay % 9) && ((mode_info->vdisplay * 16 / 9) == mode_info->hdisplay))
        vsync = 5;
    else if (!(mode_info->vdisplay % 10) && ((mode_info->vdisplay * 16 / 10) == mode_info->hdisplay))
        vsync = 6;
    else if (!(mode_info->vdisplay % 4) && ((mode_info->vdisplay * 5 / 4) == mode_info->hdisplay))
        vsync = 7;
    else if (!(mode_info->vdisplay % 9) && ((mode_info->vdisplay * 15 / 9) == mode_info->hdisplay))
        vsync = 7;
    else                        /* Custom */
        vsync = 10;

    {             /* simplified GTF calculation */

        /* 4) Minimum time of vertical sync + back porch interval (µs)
         * default 550.0 */
#define CVT_MIN_VSYNC_BP 550.0

        /* 3) Nominal HSync width (% of line period) - default 8 */
#define CVT_HSYNC_PERCENTAGE 8

        float hblank_percentage;
        int vsync_and_back_porch, vback_porch;
        int hblank, hsync_w;

        /* 8. Estimated Horizontal period */
        hperiod = ((float) (1000000.0 / vfield_rate - CVT_MIN_VSYNC_BP)) /
            (vdisplay_rnd + 2 * vmargin + CVT_MIN_V_PORCH_RND + interlace);

        /* 9. Find number of lines in sync + backporch */
        if (((int) (CVT_MIN_VSYNC_BP / hperiod) + 1) <
            (vsync + CVT_MIN_V_BPORCH))
            vsync_and_back_porch = vsync + CVT_MIN_V_BPORCH;
        else
            vsync_and_back_porch = (int) (CVT_MIN_VSYNC_BP / hperiod) + 1;

        /* 10. Find number of lines in back porch */
        vback_porch = vsync_and_back_porch - vsync;
        (void) vback_porch;

        /* 11. Find total number of lines in vertical field */
        mode_info->vtotal =
            vdisplay_rnd + 2 * vmargin + vsync_and_back_porch + interlace +
            CVT_MIN_V_PORCH_RND;

        /* 5) Definition of Horizontal blanking time limitation */
        /* Gradient (%/kHz) - default 600 */
#define CVT_M_FACTOR 600

        /* Offset (%) - default 40 */
#define CVT_C_FACTOR 40

        /* Blanking time scaling factor - default 128 */
#define CVT_K_FACTOR 128

        /* Scaling factor weighting - default 20 */
#define CVT_J_FACTOR 20

#define CVT_M_PRIME CVT_M_FACTOR * CVT_K_FACTOR / 256
#define CVT_C_PRIME (CVT_C_FACTOR - CVT_J_FACTOR) * CVT_K_FACTOR / 256 + \
        CVT_J_FACTOR

        /* 12. Find ideal blanking duty cycle from formula */
        hblank_percentage = CVT_C_PRIME - CVT_M_PRIME * hperiod / 1000.0;

        /* 13. Blanking time */
        if (hblank_percentage < 20)
            hblank_percentage = 20;

        hblank = mode_info->hdisplay * hblank_percentage / (100.0 - hblank_percentage);
        hblank -= hblank % (2 * CVT_H_GRANULARITY);

        /* 14. Find total number of pixels in a line. */
        mode_info->htotal = mode_info->hdisplay + hblank;

        /* Fill in HSync values */
        mode_info->hsync_end = mode_info->hdisplay + hblank / 2;

        hsync_w = (mode_info->htotal * CVT_HSYNC_PERCENTAGE) / 100;
        hsync_w -= hsync_w % CVT_H_GRANULARITY;
        mode_info->hsync_start = mode_info->hsync_end - hsync_w;

        /* Fill in vsync values */
        mode_info->vsync_start = mode_info->vdisplay + CVT_MIN_V_PORCH_RND;
        mode_info->vsync_end = mode_info->vsync_start + vsync;

    }

    /* 15/13. Find pixel clock frequency (kHz for xf86) */
    mode_info->dot_clock = mode_info->htotal * 1000.0 / hperiod;
    mode_info->dot_clock -= mode_info->dot_clock % CVT_CLOCK_STEP;

    /* 16/14. Find actual Horizontal Frequency (kHz) */
    mode_info->hsync = ((float) mode_info->dot_clock) / ((float) mode_info->htotal);

    /* 17/15. Find actual Field rate */
    mode_info->vrefresh = (1000.0 * ((float) mode_info->dot_clock)) /
        ((float) (mode_info->htotal * mode_info->vtotal));

    /* 18/16. Find actual vertical frame frequency */
    /* ignore - we don't do interlace here */
}

void
bochs_vbe_recalctimings(svga_t* svga)
{
    bochs_vbe_t  *bochs_vbe  = (bochs_vbe_t *) svga->priv;
    uint32_t maxy = 0;

    if (bochs_vbe->vbe_regs[VBE_DISPI_INDEX_ENABLE] & VBE_DISPI_ENABLED) {
        struct vbe_mode_info mode = {};
        svga->bpp = bochs_vbe->vbe_regs[VBE_DISPI_INDEX_BPP];
        bochs_vbe->vbe_regs[VBE_DISPI_INDEX_XRES] &= ~7;
        if (bochs_vbe->vbe_regs[VBE_DISPI_INDEX_XRES] == 0) {
            bochs_vbe->vbe_regs[VBE_DISPI_INDEX_XRES] = 8;
        }
        if (bochs_vbe->vbe_regs[VBE_DISPI_INDEX_VIRT_WIDTH] > VBE_DISPI_MAX_XRES) {
            bochs_vbe->vbe_regs[VBE_DISPI_INDEX_VIRT_WIDTH] = VBE_DISPI_MAX_XRES;
        }
        if (bochs_vbe->vbe_regs[VBE_DISPI_INDEX_VIRT_WIDTH] < bochs_vbe->vbe_regs[VBE_DISPI_INDEX_XRES]) {
            bochs_vbe->vbe_regs[VBE_DISPI_INDEX_VIRT_WIDTH] = bochs_vbe->vbe_regs[VBE_DISPI_INDEX_XRES];
        }
        if (bochs_vbe->vbe_regs[VBE_DISPI_INDEX_X_OFFSET] > VBE_DISPI_MAX_XRES) {
            bochs_vbe->vbe_regs[VBE_DISPI_INDEX_X_OFFSET] = VBE_DISPI_MAX_XRES;
        }
        if (bochs_vbe->vbe_regs[VBE_DISPI_INDEX_Y_OFFSET] > VBE_DISPI_MAX_YRES) {
            bochs_vbe->vbe_regs[VBE_DISPI_INDEX_Y_OFFSET] = VBE_DISPI_MAX_YRES;
        }

        if (bochs_vbe->vbe_regs[VBE_DISPI_INDEX_YRES] == 0) {
            bochs_vbe->vbe_regs[VBE_DISPI_INDEX_YRES] = 1;
        }
        if (bochs_vbe->vbe_regs[VBE_DISPI_INDEX_YRES] > VBE_DISPI_MAX_YRES) {
            bochs_vbe->vbe_regs[VBE_DISPI_INDEX_YRES] = VBE_DISPI_MAX_YRES;
        }
        gen_mode_info(bochs_vbe->vbe_regs[VBE_DISPI_INDEX_XRES], bochs_vbe->vbe_regs[VBE_DISPI_INDEX_YRES], 72.f, &mode);
        svga->char_width = 1;
        svga->dots_per_clock = 1;
        svga->clock = (cpuclock * (double) (1ULL << 32)) / (mode.dot_clock * 1000.);
        svga->dispend = mode.vdisplay;
        svga->hdisp = mode.hdisplay;
        svga->vsyncstart = mode.vsync_start;
        svga->vtotal = mode.vtotal;
        svga->htotal = mode.htotal;
        svga->hblankstart = mode.hdisplay;
        svga->hblankend = mode.hdisplay + (mode.htotal - mode.hdisplay - 1);
        svga->vblankstart = svga->dispend; /* no vertical overscan. */
        svga->rowcount = 0;
        if (bochs_vbe->vbe_regs[VBE_DISPI_INDEX_BPP] != 4) {
            svga->fb_only = 1;
            svga->adv_flags |= FLAG_NO_SHIFT3;
            
        } else {
            svga->fb_only = 0;
            svga->adv_flags &= ~FLAG_NO_SHIFT3;
        }

        svga->bpp = bochs_vbe->vbe_regs[VBE_DISPI_INDEX_BPP];

        if (svga->bpp == 4) {
            svga->rowoffset = bochs_vbe->vbe_regs[VBE_DISPI_INDEX_VIRT_WIDTH] >> 3;
            svga->ma_latch = (bochs_vbe->vbe_regs[VBE_DISPI_INDEX_Y_OFFSET] * svga->rowoffset) + (bochs_vbe->vbe_regs[VBE_DISPI_INDEX_X_OFFSET] >> 3);
        } else {
            svga->rowoffset = bochs_vbe->vbe_regs[VBE_DISPI_INDEX_VIRT_WIDTH] * (svga->bpp / 8);
            svga->ma_latch = (bochs_vbe->vbe_regs[VBE_DISPI_INDEX_Y_OFFSET] * svga->rowoffset) + (bochs_vbe->vbe_regs[VBE_DISPI_INDEX_X_OFFSET] * (svga->bpp / 8));
        }

        if ((svga->ma_latch + bochs_vbe->vbe_regs[VBE_DISPI_INDEX_YRES] * svga->rowoffset) > svga->vram_max) {
            bochs_vbe->vbe_regs[VBE_DISPI_INDEX_Y_OFFSET] = 0;
            svga->ma_latch = (svga->bpp == 4) ? (bochs_vbe->vbe_regs[VBE_DISPI_INDEX_X_OFFSET] >> 3) : (bochs_vbe->vbe_regs[VBE_DISPI_INDEX_X_OFFSET] * (svga->bpp / 8));
            if ((svga->ma_latch + bochs_vbe->vbe_regs[VBE_DISPI_INDEX_YRES] * svga->rowoffset) > svga->vram_max) {
                svga->ma_latch = 0;
                bochs_vbe->vbe_regs[VBE_DISPI_INDEX_X_OFFSET] = 0;
            }
        }
        svga->split = 0xFFFFFF;

        switch (svga->bpp)
        {
        case 4:
            svga->render = svga_render_4bpp_highres;
            break;
        default:
        case 8:
            svga->render = svga_render_8bpp_clone_highres;
            break;
        case 15:
            svga->render = svga_render_15bpp_highres;
            break;
        case 16:
            svga->render = svga_render_16bpp_highres;
            break;
        case 24:
            svga->render = svga_render_24bpp_highres;
            break;
        case 32:
            svga->render = svga_render_32bpp_highres;
            break;
        }
    } else {
        svga->fb_only = 0;
        svga->packed_4bpp = 0;
        svga->adv_flags &= ~FLAG_NO_SHIFT3;
    }
}

uint16_t
bochs_vbe_inw(uint16_t addr, void *priv)
{
    bochs_vbe_t  *bochs_vbe  = (bochs_vbe_t *) priv;
    bool vbe_get_caps = !!(bochs_vbe->vbe_regs[VBE_DISPI_INDEX_ENABLE] & VBE_DISPI_GETCAPS);
    if (addr == 0x1ce)
        return bochs_vbe->vbe_index;

    switch (bochs_vbe->vbe_index) {
        case VBE_DISPI_INDEX_XRES:
        {
            return vbe_get_caps ? VBE_DISPI_MAX_XRES : bochs_vbe->vbe_regs[bochs_vbe->vbe_index];
        }
        case VBE_DISPI_INDEX_YRES:
        {
            return vbe_get_caps ? VBE_DISPI_MAX_YRES : bochs_vbe->vbe_regs[bochs_vbe->vbe_index];
        }
        case VBE_DISPI_INDEX_BPP:
        {
            return vbe_get_caps ? 32 : bochs_vbe->vbe_regs[bochs_vbe->vbe_index];
        }
        case VBE_DISPI_INDEX_VIDEO_MEMORY_64K:
        {
            return bochs_vbe->svga.vram_max >> 16;
        }
        case VBE_DISPI_INDEX_BANK:
        {
            return vbe_get_caps ? (VBE_DISPI_BANK_GRANULARITY_32K << 8) : bochs_vbe->vbe_regs[bochs_vbe->vbe_index];
        }
        case VBE_DISPI_INDEX_DDC:
        {
            if (bochs_vbe->vbe_regs[bochs_vbe->vbe_index] & (1 << 7)) {
                uint16_t ret = bochs_vbe->vbe_regs[bochs_vbe->vbe_index] & ((1 << 7) | 0x3);
                ret |= i2c_gpio_get_scl(bochs_vbe->i2c) << 2;
                ret |= i2c_gpio_get_sda(bochs_vbe->i2c) << 3;
                return ret;
            } else {
                return 0x000f;
            }
        }
        default:
            return bochs_vbe->vbe_regs[bochs_vbe->vbe_index];
    }

    return bochs_vbe->vbe_regs[bochs_vbe->vbe_index];
}

void
bochs_vbe_outw(uint16_t addr, uint16_t val, void *priv)
{
    bochs_vbe_t  *bochs_vbe  = (bochs_vbe_t *) priv;
    if (addr == 0x1ce) {
        bochs_vbe->vbe_index = val;
        return;
    } else if (addr == 0x1cf || addr == 0x1d0) {
        switch (bochs_vbe->vbe_index) {
            case VBE_DISPI_INDEX_ID:
                if (val == VBE_DISPI_ID0 ||
                    val == VBE_DISPI_ID1 ||
                    val == VBE_DISPI_ID2 ||
                    val == VBE_DISPI_ID3 ||
                    val == VBE_DISPI_ID4 ||
                    val == VBE_DISPI_ID5) {
                    bochs_vbe->vbe_regs[bochs_vbe->vbe_index] = val;
                }
                break;
            case VBE_DISPI_INDEX_XRES:
            case VBE_DISPI_INDEX_YRES:
            case VBE_DISPI_INDEX_BPP:
            case VBE_DISPI_INDEX_VIRT_WIDTH:
            case VBE_DISPI_INDEX_X_OFFSET:
            case VBE_DISPI_INDEX_Y_OFFSET:
                bochs_vbe->vbe_regs[bochs_vbe->vbe_index] = val;
                svga_recalctimings(&bochs_vbe->svga);
                break;

            case VBE_DISPI_INDEX_BANK: 
            {
                uint16_t rw_mode = VBE_DISPI_BANK_RW; // compatibility mode
                if ((val & VBE_DISPI_BANK_RW) != 0) {
                    rw_mode = (val & VBE_DISPI_BANK_RW);
                }
                if (val & VBE_DISPI_BANK_RD) {
                    bochs_vbe->svga.read_bank = (val & 0x1ff) * (bochs_vbe->bank_gran << 10);
                }
                if (val & VBE_DISPI_BANK_WR) {
                    bochs_vbe->svga.write_bank = (val & 0x1ff) * (bochs_vbe->bank_gran << 10);
                }
                break;
            }

            case VBE_DISPI_INDEX_DDC:
            {
                if (val & (1 << 7)) {
                    i2c_gpio_set(bochs_vbe->i2c, !!(val & 1), !!(val & 2));
                    bochs_vbe->vbe_regs[bochs_vbe->vbe_index] = val;
                } else {
                    bochs_vbe->vbe_regs[bochs_vbe->vbe_index] &= ~(1 << 7);
                }
                break;
            }

            case VBE_DISPI_INDEX_ENABLE: {
                uint32_t new_bank_gran = 64;
                bochs_vbe->vbe_regs[bochs_vbe->vbe_index] = val;
                if ((val & VBE_DISPI_ENABLED) && !(bochs_vbe->vbe_regs[VBE_DISPI_ENABLED] & VBE_DISPI_ENABLED)) {
                    bochs_vbe->vbe_regs[bochs_vbe->vbe_index] = val;
                    bochs_vbe->vbe_regs[VBE_DISPI_INDEX_X_OFFSET] = 0;
                    bochs_vbe->vbe_regs[VBE_DISPI_INDEX_Y_OFFSET] = 0;
                    bochs_vbe->vbe_regs[VBE_DISPI_INDEX_VIRT_WIDTH] = 0;
                    svga_recalctimings(&bochs_vbe->svga);
                    if (!(val & VBE_DISPI_NOCLEARMEM)) {
                        memset(bochs_vbe->svga.vram, 0,
                            bochs_vbe->vbe_regs[VBE_DISPI_INDEX_YRES] * bochs_vbe->svga.rowoffset);
                    }
                } else {
                    bochs_vbe->svga.read_bank = bochs_vbe->svga.write_bank = 0;
                }
                if ((val & VBE_DISPI_BANK_GRANULARITY_32K) != 0) {
                    new_bank_gran = 32;
                } else {
                    new_bank_gran = 64;
                }
                if (bochs_vbe->bank_gran != new_bank_gran) {
                    bochs_vbe->bank_gran = new_bank_gran;
                    bochs_vbe->svga.read_bank = bochs_vbe->svga.write_bank = 0;
                }
                if (val & VBE_DISPI_8BIT_DAC)
                    bochs_vbe->svga.adv_flags &= ~FLAG_RAMDAC_SHIFT; 
                else
                    bochs_vbe->svga.adv_flags |= FLAG_RAMDAC_SHIFT;
                bochs_vbe->vbe_regs[bochs_vbe->vbe_index] &= ~VBE_DISPI_NOCLEARMEM;
            }
        }
    }
}

void
bochs_vbe_out(uint16_t addr, uint8_t val, void *priv)
{
    bochs_vbe_t  *bochs_vbe  = (bochs_vbe_t *) priv;
    svga_t *svga = &bochs_vbe->svga;
    uint8_t old;

    if (((addr & 0xfff0) == 0x3d0 || (addr & 0xfff0) == 0x3b0) && !(svga->miscout & 1))
        addr ^= 0x60;

    switch (addr) {
        case VBE_DISPI_IOPORT_INDEX:
            bochs_vbe->vbe_index = val;
            break;
        case VBE_DISPI_IOPORT_DATA:
            return bochs_vbe_outw(0x1cf, val | (bochs_vbe_inw(0x1cf, bochs_vbe) & 0xFF00), bochs_vbe);
        case VBE_DISPI_IOPORT_DATA + 1:
            return bochs_vbe_outw(0x1cf, (val << 8) | (bochs_vbe_inw(0x1cf, bochs_vbe) & 0xFF), bochs_vbe);
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

        default:
            break;
    }
    svga_out(addr, val, svga);
}

uint8_t
bochs_vbe_in(uint16_t addr, void *priv)
{
    bochs_vbe_t  *bochs_vbe  = (bochs_vbe_t *) priv;
    svga_t *svga = &bochs_vbe->svga;
    uint8_t temp;

    if (((addr & 0xfff0) == 0x3d0 || (addr & 0xfff0) == 0x3b0) && !(svga->miscout & 1))
        addr ^= 0x60;

    switch (addr) {
        case VBE_DISPI_IOPORT_INDEX:
            return bochs_vbe->vbe_index;
        case VBE_DISPI_IOPORT_DATA:
            return bochs_vbe_inw(0x1cf, bochs_vbe);
        case VBE_DISPI_IOPORT_DATA + 1:
            return bochs_vbe_inw(0x1cf, bochs_vbe) >> 8;
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
bochs_vbe_pci_read(int func, int addr, void *p)
{
    bochs_vbe_t *bochs_vbe = (bochs_vbe_t *) p;

    {
        switch (addr) {
            case 0x00:
                return 0x34;
            case 0x01:
                return 0x12;
            case 0x02:
                return 0x11;
            case 0x03:
                return 0x11;
            case 0x04:
                return (bochs_vbe->pci_conf_status & 0b11100011) | 0x80;
            case 0x06:
                return 0x80;
            case 0x07:
                return 0x02;
            case 0x08:
            case 0x09:
            case 0x0a:
                return 0x00;
            case 0x0b:
                return 0x03;
            case 0x13:
                return bochs_vbe->linear_mapping.base >> 24;
            case 0x30:
                return bochs_vbe->pci_rom_enable & 0x1;
            case 0x31:
                return 0x0;
            case 0x32:
                return bochs_vbe->rom_addr & 0xFF;
            case 0x33:
                return (bochs_vbe->rom_addr & 0xFF00) >> 8;
            case 0x3c:
                return bochs_vbe->pci_line_interrupt;
            case 0x3d:
                return 0x01;
            case 0x2C:
            case 0x2D:
                return 0;
            case 0x2E:
            case 0x2F:
                return 0;
            default:
                return 0x00;
        }
    }
}

static void
bochs_vbe_pci_write(int func, int addr, uint8_t val, void *p)
{
    bochs_vbe_t *bochs_vbe = (bochs_vbe_t *) p;

    {
        switch (addr) {
            case 0x04:
                {
                    bochs_vbe->pci_conf_status = val;
                    io_removehandler(0x03c0, 0x0020, bochs_vbe_in, NULL, NULL, bochs_vbe_out, NULL, NULL, bochs_vbe);
                    io_removehandler(0x1ce, 3, bochs_vbe_in, bochs_vbe_inw, 0, bochs_vbe_out, bochs_vbe_outw, 0, bochs_vbe);
                    mem_mapping_disable(&bochs_vbe->linear_mapping);
                    mem_mapping_disable(&bochs_vbe->svga.mapping);
                    if (bochs_vbe->pci_conf_status & PCI_COMMAND_IO) {
                        io_sethandler(0x03c0, 0x0020, bochs_vbe_in, NULL, NULL, bochs_vbe_out, NULL, NULL, bochs_vbe);
                        io_sethandler(0x1ce, 3, bochs_vbe_in, bochs_vbe_inw, 0, bochs_vbe_out, bochs_vbe_outw, 0, bochs_vbe);
                    }
                    if (bochs_vbe->pci_conf_status & PCI_COMMAND_MEM) {
                        mem_mapping_enable(&bochs_vbe->svga.mapping);
                        if (bochs_vbe->linear_mapping.base)
                            mem_mapping_enable(&bochs_vbe->linear_mapping);
                    }
                    break;
                }
            case 0x13:
                {
                    if (!bochs_vbe->linear_mapping.enable) {
                        bochs_vbe->linear_mapping.base = val << 24;
                        break;
                    }
                    mem_mapping_set_addr(&bochs_vbe->linear_mapping, val << 24, (1 << 24));
                    break;
                }
            case 0x3c:
                bochs_vbe->pci_line_interrupt = val;
                break;
            case 0x30:
                bochs_vbe->pci_rom_enable = val & 0x1;
                mem_mapping_disable(&bochs_vbe->bios_rom.mapping);
                if (bochs_vbe->pci_rom_enable & 1) {
                    mem_mapping_set_addr(&bochs_vbe->bios_rom.mapping, bochs_vbe->rom_addr << 16, 0x10000);
                }
                break;
            case 0x32:
                bochs_vbe->rom_addr &= ~0xFF;
                bochs_vbe->rom_addr |= val & 0xFC;
                if (bochs_vbe->pci_rom_enable & 1) {
                    mem_mapping_set_addr(&bochs_vbe->bios_rom.mapping, bochs_vbe->rom_addr << 16, 0x10000);
                }
                break;
            case 0x33:
                bochs_vbe->rom_addr &= ~0xFF00;
                bochs_vbe->rom_addr |= (val << 8);
                if (bochs_vbe->pci_rom_enable & 1) {
                    mem_mapping_set_addr(&bochs_vbe->bios_rom.mapping, bochs_vbe->rom_addr << 16, 0x10000);
                }
        }
    }
}

static void *
bochs_vbe_init(const device_t *info)
{
    bochs_vbe_t *bochs_vbe = malloc(sizeof(bochs_vbe_t));
    memset(bochs_vbe, 0, sizeof(bochs_vbe_t));

    rom_init(&bochs_vbe->bios_rom, "roms/video/bochs/VGABIOS-lgpl-latest.bin", 0xc0000, 0x10000, 0xffff, 0x0000, MEM_MAPPING_EXTERNAL);

    video_inform(VIDEO_FLAG_TYPE_SPECIAL, &timing_bochs);

    svga_init(info, &bochs_vbe->svga, bochs_vbe, 1 << 24, /*16mb*/
              bochs_vbe_recalctimings,
              bochs_vbe_in, bochs_vbe_out,
              NULL,
              NULL);

    io_sethandler(0x03c0, 0x0020, bochs_vbe_in, NULL, NULL, bochs_vbe_out, NULL, NULL, bochs_vbe);
    io_sethandler(0x1ce, 3, bochs_vbe_in, bochs_vbe_inw, 0, bochs_vbe_out, bochs_vbe_outw, 0, bochs_vbe);
    
    mem_mapping_disable(&bochs_vbe->bios_rom.mapping);
    mem_mapping_add(&bochs_vbe->linear_mapping, 0, 0, svga_readb_linear, svga_readw_linear, svga_readl_linear, svga_writeb_linear, svga_writew_linear, svga_writel_linear, NULL, MEM_MAPPING_EXTERNAL, &bochs_vbe->svga);

    bochs_vbe->svga.bpp     = 8;
    bochs_vbe->svga.miscout = 1;
    bochs_vbe->bank_gran    = 64;

    svga_set_ramdac_type(&bochs_vbe->svga, RAMDAC_8BIT);
    bochs_vbe->svga.adv_flags |= FLAG_RAMDAC_SHIFT;
    bochs_vbe->svga.decode_mask = 0xffffff;

    bochs_vbe->i2c = i2c_gpio_init("ddc_bochs");
    bochs_vbe->ddc = ddc_init(i2c_gpio_get_bus(bochs_vbe->i2c));

    pci_add_card(PCI_ADD_VIDEO, bochs_vbe_pci_read, bochs_vbe_pci_write, bochs_vbe, &bochs_vbe->slot);

    return bochs_vbe;
}

static int
bochs_vbe_available(void)
{
    return rom_present("roms/video/bochs/VGABIOS-lgpl-latest.bin");
}

void
bochs_vbe_close(void *priv)
{
    bochs_vbe_t *bochs_vbe = (bochs_vbe_t *) priv;

    ddc_close(bochs_vbe->ddc);
    i2c_gpio_close(bochs_vbe->i2c);

    svga_close(&bochs_vbe->svga);

    free(bochs_vbe);
}

void
bochs_vbe_speed_changed(void *priv)
{
    bochs_vbe_t *bochs_vbe = (bochs_vbe_t *) priv;

    svga_recalctimings(&bochs_vbe->svga);
}

void
bochs_vbe_force_redraw(void *priv)
{
    bochs_vbe_t *bochs_vbe = (bochs_vbe_t *) priv;

    bochs_vbe->svga.fullchange = changeframecount;
}

const device_t bochs_svga_device = {
    .name          = "Bochs SVGA",
    .internal_name = "bochs_svga",
    .flags         = DEVICE_PCI,
    .local         = 0,
    .init          = bochs_vbe_init,
    .close         = bochs_vbe_close,
    .reset         = NULL,
    { .available = bochs_vbe_available },
    .speed_changed = bochs_vbe_speed_changed,
    .force_redraw  = bochs_vbe_force_redraw,
    .config        = NULL
};

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

#include "cvt/libxcvt.h"

#define VBE_DISPI_BANK_ADDRESS           0xA0000
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

#define VBE_DISPI_LFB_PHYSICAL_ADDRESS   0xE0000000

static video_timings_t timing_ps1_svga_isa = { .type = VIDEO_ISA, .write_b = 6, .write_w = 8, .write_l = 16, .read_b = 6, .read_w = 8, .read_l = 16 };
static video_timings_t timing_ps1_svga_mca = { .type = VIDEO_MCA, .write_b = 6, .write_w = 8, .write_l = 16, .read_b = 6, .read_w = 8, .read_l = 16 };

typedef struct bochs_vbe_t {
    svga_t svga;

    rom_t bios_rom;
    uint16_t vbe_regs[16];
    uint16_t vbe_index;

    uint16_t bank_gran;
} bochs_vbe_t;

void
bochs_vbe_recalctimings(svga_t* svga)
{
    bochs_vbe_t  *bochs_vbe  = (bochs_vbe_t *) svga->priv;
    uint32_t maxy = 0;

    if (bochs_vbe->vbe_regs[VBE_DISPI_INDEX_ENABLE] & VBE_DISPI_ENABLED) {
        struct libxcvt_mode_info* mode = libxcvt_gen_mode_info(bochs_vbe->vbe_regs[VBE_DISPI_INDEX_XRES], bochs_vbe->vbe_regs[VBE_DISPI_INDEX_YRES], 72.f, false, false);
        if (!mode) {
            fatal("bochs_vbe: Out of memory!\n");
        }
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


        /* Is this okay? */
        svga->char_width = 1;
        svga->dots_per_clock = 1;
        svga->clock = mode->dot_clock * 1000.;
        svga->dispend = mode->vdisplay;
        svga->hdisp = mode->hdisplay;
        svga->vsyncstart = mode->vsync_start;
        svga->vtotal = mode->vtotal;
        svga->htotal = mode->htotal;
        svga->hblankstart = mode->hdisplay;
        svga->hblankend = mode->hdisplay + (mode->htotal - mode->hdisplay - 1);
        svga->vblankstart = svga->dispend; /* no vertical overscan. */
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
            svga->render = svga_render_8bpp_highres;
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
        free(mode);
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

            case VBE_DISPI_INDEX_ENABLE: {
                uint32_t new_bank_gran = 64;
                if ((val & VBE_DISPI_ENABLED) && !(bochs_vbe->vbe_regs[VBE_DISPI_ENABLED] & VBE_DISPI_ENABLED)) {
                    bochs_vbe->vbe_regs[bochs_vbe->vbe_index] = (val & VBE_DISPI_ENABLED);
                    bochs_vbe->vbe_regs[VBE_DISPI_INDEX_X_OFFSET] = 0;
                    bochs_vbe->vbe_regs[VBE_DISPI_INDEX_Y_OFFSET] = 0;
                    bochs_vbe->vbe_regs[VBE_DISPI_INDEX_VIRT_WIDTH] = 0;
                    svga_recalctimings(&bochs_vbe->svga);
                    if (!(val & VBE_DISPI_NOCLEARMEM)) {
                        memset(bochs_vbe->svga.vram, 0,
                            bochs_vbe->vbe_regs[VBE_DISPI_INDEX_YRES] * bochs_vbe->svga.rowoffset);
                    }

                    break;
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
        case 0x1CE:
            bochs_vbe->vbe_index = val;
            break;
        case 0x1CF:
            return bochs_vbe_outw(0x1cf, val | (bochs_vbe_inw(0x1cf, bochs_vbe) & 0xFF00), bochs_vbe);
        case 0x1D0:
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
        case 0x1CE:
            return bochs_vbe->vbe_index;
        case 0x1CF:
            return bochs_vbe_inw(0x1cf, bochs_vbe);
        case 0x1D0:
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

static void *
bochs_vbe_init(const device_t *info)
{
    bochs_vbe_t *bochs_vbe = malloc(sizeof(bochs_vbe_t));
    memset(bochs_vbe, 0, sizeof(bochs_vbe_t));

    rom_init(&bochs_vbe->bios_rom, "roms/video/bochs/bochs.bin", 0xc0000, 0x8000, 0x7fff, 0x2000, MEM_MAPPING_EXTERNAL);

    video_inform(VIDEO_FLAG_TYPE_SPECIAL, &timing_ps1_svga_isa);

    svga_init(info, &bochs_vbe->svga, bochs_vbe, 1 << 24, /*16mb*/
              bochs_vbe_recalctimings,
              bochs_vbe_in, bochs_vbe_out,
              NULL,
              NULL);

    io_sethandler(0x03c0, 0x0020, bochs_vbe_in, NULL, NULL, bochs_vbe_out, NULL, NULL, bochs_vbe);
    io_sethandler(0x1ce, 3, bochs_vbe_in, bochs_vbe_inw, 0, bochs_vbe_out, bochs_vbe_outw, 0, bochs_vbe);

    bochs_vbe->svga.bpp     = 8;
    bochs_vbe->svga.miscout = 1;

    return bochs_vbe;
}

static int
bochs_vbe_available(void)
{
    return rom_present("roms/video/bochs/bochs.bin");
}

void
bochs_vbe_close(void *priv)
{
    bochs_vbe_t *vga = (bochs_vbe_t *) priv;

    svga_close(&vga->svga);

    free(vga);
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

const device_t vga_device = {
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

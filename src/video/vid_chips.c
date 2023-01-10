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

static video_timings_t timing_ps1_svga_isa = { .type = VIDEO_ISA, .write_b = 6, .write_w = 8, .write_l = 16, .read_b = 6, .read_w = 8, .read_l = 16 };

typedef struct chips_vid_t {
    svga_t svga;

    uint8_t ext_regs[0x100];
    uint8_t ext_setup;
    uint8_t setup;
    uint8_t ext_index;
    rom_t bios_rom;
} chips_vid_t;

void
chips_update_segment_mapping(chips_vid_t* chips)
{
    uint8_t memory_map_mode = (chips->svga.gdcreg[6] >> 2) & 3;
    uint32_t old_extra_bank = chips->svga.extra_banks[0];
    uint32_t old_extra_bank_2 = chips->svga.extra_banks[1];

    chips->svga.read_bank = chips->svga.write_bank = 0;
    chips->svga.extra_banks[0] = chips->svga.extra_banks[1] = 0;
    chips->svga.adv_flags &= ~FLAG_EXTRA_BANKS_HALF;
    chips->svga.packed_chain4 = (chips->ext_regs[0x0B] & 0x4);
    if (chips->ext_regs[0x0B] & 0x1) {
        if ((chips->ext_regs[0xB] & 0x2) && memory_map_mode <= 1) { /* Dual map */
            chips->svga.adv_flags |= FLAG_EXTRA_BANKS_HALF;

            chips->svga.extra_banks[0] = ((!chips->svga.chain4) ? 1024 : 4096) * (chips->ext_regs[0x10]);
            chips->svga.extra_banks[1] = ((!chips->svga.chain4) ? 1024 : 4096) * (chips->ext_regs[0x11]);
            pclog("[%04X:%08X] C&TVID: 1st half of bank @ 0x%05X, 2nd half of bank @ 0x%05X\n", CS, cpu_state.pc, chips->svga.extra_banks[0], chips->svga.extra_banks[1]);
        } else { /* Single map */
            chips->svga.read_bank = ((!chips->svga.chain4) ? 1024 : 4096) * chips->ext_regs[0x10];
            chips->svga.write_bank = ((!chips->svga.chain4) ? 1024 : 4096) * chips->ext_regs[0x10];
            //pclog("C&TVID: Bank @ 0x%05X\n", chips->svga.read_bank);
        }
    }
}

uint8_t
chips_ext_read(chips_vid_t* chips, uint8_t data)
{
    uint8_t ret = 0xFF;
    static uint8_t ver = 0x40;
    if ((data & 0x1) == 0x00)
        return chips->ext_index;
    else {
        switch (chips->ext_index) {
            case 0x00:
                ret = ver;
                //pclog("0x%X <- 0x%X (version read)\n", ret, chips->ext_index);
                break;
            case 0x01:
                {
                    ret = 0x3;
                    //pclog("0x%X <- 0x%X (config read)\n", ret, chips->ext_index);
                    break;
                }
            default:
                ret = chips->ext_regs[chips->ext_index];
                //pclog("0x%X <- 0x%X\n", ret, chips->ext_index);
                break;
        }
    }
    
    return ret;
}

void
chips_ext_write(chips_vid_t* chips, uint8_t data, uint8_t val)
{
    if ((data & 0x1) == 0x00)
        chips->ext_index = val;
    else {
        chips->ext_regs[chips->ext_index] = val;
        //pclog("[%04X:%08X] 0x%X -> 0x%X\n", CS, cpu_state.pc, val, chips->ext_index);
        switch (chips->ext_index) {
            case 0x28: /* Video Interface Register */
                svga_recalctimings(&chips->svga);
                break;
            case 0x0C: /* Start Address Top Register */
                svga_recalctimings(&chips->svga);
                break;
            case 0x0B:
		pclog("[%04X:%08X] Write 0x%X to bank register\n", CS, cpu_state.pc, val);
            case 0x10:
            case 0x11:
                chips_update_segment_mapping(chips);
                break;
            case 0x3:
            case 0x5 ... 0xA:
            case 0xD ... 0xF:
            case 0x12:
            case 0x13:
            case 0x16:
            case 0x17:
            case 0x20 ... 0x27:
            case 0x29 ... 0x7D:
                pclog("Unimplemented register 0x%X\n", chips->ext_index);
                break;
            default:
                break;
        }
    }
}

void
chips_out(uint16_t addr, uint8_t val, void *p)
{
    chips_vid_t  *chips  = (chips_vid_t *) p;
    svga_t *svga = &chips->svga;
    uint8_t old;

    if (((addr & 0xfff0) == 0x3d0 || (addr & 0xfff0) == 0x3b0) && !(svga->miscout & 1))
        addr ^= 0x60;

    switch (addr) {
        case 0x3C2:
            //if (chips->ext_regs[0x15] & 0x10)
            //    return;
            break;
        case 0x3C6 ... 0x3C9:
            if (chips->ext_regs[0x15] & 0x20)
                return;
            break;
        case 0x3C5:
        {
            if (svga->seqaddr == 4) {
                svga_out(addr, val, svga);
                chips_update_segment_mapping(chips);
                return;
            }
            break;
        }
        case 0x3CF:
        {
            if (svga->gdcaddr == 6) {
                svga_out(addr, val, svga);
                chips_update_segment_mapping(chips);
                return;
            }
            break;
        }
        case 0x3B6:
        case 0x3B7:
            if (!!(chips->ext_setup & (1 << 6)) && ((chips->ext_setup & 0x80) || (chips->ext_regs[0x70] & 0x80))) {
                chips_ext_write(chips, addr, val);
            }
            break;
        case 0x3D6:
        case 0x3D7:
            if (!(chips->ext_setup & (1 << 6)) && ((chips->ext_setup & 0x80) || (chips->ext_regs[0x70] & 0x80))) {
                chips_ext_write(chips, addr, val);
            }
            break;
        case 0x3D4:
            svga->crtcreg = val & 0x3f;
            return;
        case 0x3D5:
            if (svga->crtcreg & 0x20)
                return;
            if ((svga->crtcreg < 7) && ((svga->crtc[0x11] & 0x80) || (chips->ext_regs[0x15] & 0x40)))
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
chips_in(uint16_t addr, void *p)
{
    chips_vid_t  *chips  = (chips_vid_t *) p;
    svga_t *svga = &chips->svga;
    uint8_t temp;

    if (((addr & 0xfff0) == 0x3d0 || (addr & 0xfff0) == 0x3b0) && !(svga->miscout & 1))
        addr ^= 0x60;

    switch (addr) {
        case 0x3B6:
        case 0x3B7:
            if (!!(chips->ext_setup & (1 << 6)) && ((chips->ext_setup & 0x80) || (chips->ext_regs[0x70] & 0x80))) {
                temp = chips_ext_read(chips, addr);
            }
            else temp = 0xff;
            break;
        case 0x3D6:
        case 0x3D7:
            if (!(chips->ext_setup & (1 << 6)) && ((chips->ext_setup & 0x80) || (chips->ext_regs[0x70] & 0x80))) {
                temp = chips_ext_read(chips, addr);
            }
            else temp = 0xff;
            break;
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
chips_pos_in(uint16_t addr, void *p)
{
    chips_vid_t  *chips  = (chips_vid_t *) p;
    svga_t *svga = &chips->svga;
    uint8_t ret = 0xFF;

    switch (addr) {
        case 0x102:
            ret = !!(chips->setup & 0x8);
            break;
        case 0x103:
            ret = chips->ext_setup;
            break;
        case 0x104:
            ret = 0xA5;
            break;
    }
    return ret;
}

static void
chips_pos_out(uint16_t addr, uint8_t val, void *p)
{
    chips_vid_t  *chips  = (chips_vid_t *) p;
    svga_t *svga = &chips->svga;

    switch (addr) {
        case 0x102:
            chips->setup &= ~8;
            chips->setup |= (!!(val & 0x1)) << 3;
            break;
        case 0x103:
            chips->ext_setup = val;
            break;
    }
}

static void
chips_setup_out(uint16_t addr, uint8_t val, void *p)
{
    chips_vid_t  *chips  = (chips_vid_t *) p;
    svga_t *svga = &chips->svga;

    chips->setup = val;
}

static void
chips_recalctimings(svga_t *svga)
{
    chips_vid_t  *chips  = (chips_vid_t *) svga->p;

    svga->interlace = (chips->ext_regs[0x28] & 0x20);
    if (svga->interlace) {
        //svga->split = chips->ext_regs[0x19];
    }
    if (svga->bpp == 8 && (chips->ext_regs[0xB] & 1)) {
        svga->hdisp >>= 1;
        svga->render = svga_render_8bpp_highres;
    }
    svga->ma_latch |= ((chips->ext_regs[0xC] & 1) << 16); 
}

static void *
chips_init(const device_t *info)
{
    chips_vid_t *chips = malloc(sizeof(chips_vid_t));
    memset(chips, 0, sizeof(chips_vid_t));

    rom_init(&chips->bios_rom, "roms/video/chips/82C450B.VBI", 0xc0000, 0x8000, 0x7fff, 0x0000, MEM_MAPPING_EXTERNAL);

    video_inform(VIDEO_FLAG_TYPE_SPECIAL, &timing_ps1_svga_isa);

    svga_init(info, &chips->svga, chips, 1 << 19, /*512kb*/
              chips_recalctimings,
              chips_in, chips_out,
              NULL,
              NULL);

    io_sethandler(0x03b6, 0x0002, chips_in, NULL, NULL, chips_out, NULL, NULL, chips);
    io_sethandler(0x03c0, 0x0020, chips_in, NULL, NULL, chips_out, NULL, NULL, chips);
    io_sethandler(0x102, 3, chips_pos_in, NULL, NULL, chips_pos_out, NULL, NULL, chips);
    io_sethandler(0x46e8, 1, NULL, NULL, NULL, chips_setup_out, NULL, NULL, chips);

    chips->svga.bpp      = 8;
    chips->svga.miscout  = 1;

    return chips;
}

static int
chips_available(void)
{
    return rom_present("roms/video/chips/82C450B.VBI");
}

static void
chips_close(void *p)
{
    chips_vid_t *chips = (chips_vid_t *) p;

    svga_close(&chips->svga);

    free(chips);
}

static void
chips_speed_changed(void *p)
{
    chips_vid_t *chips = (chips_vid_t *) p;

    svga_recalctimings(&chips->svga);
}

void
chips_force_redraw(void *p)
{
    chips_vid_t *chips = (chips_vid_t *) p;

    chips->svga.fullchange = changeframecount;
}

const device_t chips_vid_device = {
    .name          = "C&T F82C450",
    .internal_name = "chips_vid_f82c450",
    .flags         = DEVICE_ISA | DEVICE_AT,
    .local         = 0,
    .init          = chips_init,
    .close         = chips_close,
    .reset         = NULL,
    { .available = chips_available },
    .speed_changed = chips_speed_changed,
    .force_redraw  = chips_force_redraw,
    .config        = NULL
};

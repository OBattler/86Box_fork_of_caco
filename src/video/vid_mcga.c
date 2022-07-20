/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Emulation of IBM MCGA graphics card.
 *
 *
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2008-2019 Sarah Walker.
 *		Copyright 2016-2019 Miran Grca.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include <math.h>
#include <86box/86box.h>
#include "cpu.h"
#include <86box/io.h>
#include <86box/timer.h>
#include <86box/pit.h>
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/device.h>
#include <86box/video.h>
#include <86box/vid_mcga.h>
#include <86box/vid_cga_comp.h>


#define mcga_RGB 0

static uint8_t crtcmask[32] =
{
	0xff, 0xff, 0xff, 0xff, 0x7f, 0x1f, 0x7f, 0x7f, 0xf3, 0x1f, 0x7f, 0x1f, 0x3f, 0xff, 0x3f, 0xff,
	0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static video_timings_t timing_cga = {VIDEO_ISA, 8, 16, 32,   8, 16, 32};

void mcga_recalctimings(mcga_t *cga);


void
mcga_out(uint16_t addr, uint8_t val, void *p)
{
    mcga_t *cga = (mcga_t *) p;
    uint8_t old;

    if ((addr >= 0x3d0) && (addr <= 0x3d7))
	addr = (addr & 0xff9) | 0x004;

    switch (addr) {
	case 0x3D4:
		cga->crtcreg = val & 31;
		return;
	case 0x3D5:
		/* Do nothing on horz/vert registers if write-protect is set. */
		if ((cga->crtc[0x10] & 0x80) && cga->crtcreg <= 0x7) return;
		old = cga->crtc[cga->crtcreg];
		cga->crtc[cga->crtcreg] = val & crtcmask[cga->crtcreg];
		if (old != val) {
			if ((cga->crtcreg < 0xe) || (cga->crtcreg > 0x10)) {
                cga->fullchange = changeframecount;
				mcga_recalctimings(cga);
			}
			if (cga->crtcreg == 0x10) {
 				mem_mapping_set_addr(&cga->mapping, (cga->crtc[cga->crtcreg] & 0x3) ? 0xA0000 : 0xB8000, (cga->crtc[cga->crtcreg] & 0x3) ? 64000 : 0x8000);
			}
		}
		return;
	case 0x3D8:
		old = cga->cgamode;
		cga->cgamode = val;

		if (old ^ val) {
			if ((old ^ val) & 0x05)
				update_cga16_color(val);

			mcga_recalctimings(cga);
		}
		return;
	case 0x3DD: /* Extended Mode Control Register. */
		cga->mcga_extmode = val;
		break;
	case 0x3D9:
		old = cga->cgacol;
		cga->cgacol = val;
		if (old ^ val)
			mcga_recalctimings(cga);
		return;
    }
}


uint8_t
mcga_in(uint16_t addr, void *p)
{
    mcga_t *cga = (mcga_t *) p;

    uint8_t ret = 0xff;

    if ((addr >= 0x3d0) && (addr <= 0x3d7))
	addr = (addr & 0xff9) | 0x004;

    switch (addr) {
	case 0x3D4:
		ret = cga->crtcreg;
		break;
	case 0x3D5:
		ret = (cga->crtcreg == 0x12 && (cga->crtc[0x11] & 0x80)) ? 0x2 : cga->crtc[cga->crtcreg];
		if (cga->crtcreg == 0x10) {
			ret &= 0x13;
			ret |= (cga->cgamode & 0x01) ? 0x80 : 0x00;
			ret |= (cga->cgamode & 0x01) ? 0x08 : 0x00;
		}
		break;
	case 0x3DD: /* Extended Mode Control Register. */
		ret = cga->mcga_extmode;
		break;
	case 0x3DA:
		ret = cga->cgastat;
		break;
    }

    return ret;
}


void
mcga_waitstates(void *p)
{
    int ws_array[16] = {3, 4, 5, 6, 7, 8, 4, 5, 6, 7, 8, 4, 5, 6, 7, 8};
    int ws;

    ws = ws_array[cycles & 0xf];
    cycles -= ws;
}


void
mcga_write(uint32_t addr, uint8_t val, void *p)
{
    mcga_t *cga = (mcga_t *) p;

    cga->vram[addr & 0xffff] = val;
    mcga_waitstates(cga);
}


uint8_t
mcga_read(uint32_t addr, void *p)
{
    mcga_t *cga = (mcga_t *) p;

    mcga_waitstates(cga);
    return cga->vram[addr & 0xffff];
}


void
mcga_recalctimings(mcga_t *cga)
{
    double disptime;
    double _dispontime, _dispofftime;

    if (cga->cgamode & 1) {
	disptime = (double) (cga->crtc[0] + 1);
	_dispontime = (double) cga->crtc[1];
    } else {
	disptime = (double) ((cga->crtc[0] + 1) << 1);
	_dispontime = (double) (cga->crtc[1] << 1);
    }
    _dispofftime = disptime - _dispontime;
    _dispontime = _dispontime * ((cga->crtc[0x10] & 0x10) ? VGACONST1 : CGACONST);
    _dispofftime = _dispofftime * ((cga->crtc[0x10] & 0x10) ? VGACONST1 : CGACONST);
    cga->dispontime = (uint64_t)(_dispontime);
    cga->dispofftime = (uint64_t)(_dispofftime);
}


void
mcga_poll(void *p)
{
    mcga_t *cga = (mcga_t *)p;
    uint16_t ca = (cga->crtc[15] | (cga->crtc[14] << 8)) & 0xffff;
    int drawcursor;
    int x, c, xs_temp, ys_temp;
    int oldvc;
    uint8_t chr, attr;
    uint8_t border;
    uint16_t dat;
    int cols[4];
    int col;
    int oldsc;

    if (!cga->linepos) {
	timer_advance_u64(&cga->timer, cga->dispofftime);
	cga->cgastat |= 1;
	cga->linepos = 1;
	oldsc = cga->sc;
	if ((cga->crtc[8] & 3) == 3)
		cga->sc = ((cga->sc << 1) + cga->oddeven) & 7;
	if (cga->cgadispon) {
		if (cga->displine < cga->firstline) {
			cga->firstline = cga->displine;
			video_wait_for_buffer();
		}
		cga->lastline = cga->displine;
		for (c = 0; c < 8; c++) {
			if ((cga->cgamode & 0x12) == 0x12) {
				buffer32->line[(cga->displine << 1)][c] =
				buffer32->line[(cga->displine << 1) + 1][c] = 0;
				if (cga->cgamode & 1) {
					buffer32->line[(cga->displine << 1)][c + (cga->crtc[1] << 3) + 8] =
					buffer32->line[(cga->displine << 1) + 1][c + (cga->crtc[1] << 3) + 8] = 0;
				} else {
					buffer32->line[(cga->displine << 1)][c + (cga->crtc[1] << 4) + 8] =
					buffer32->line[(cga->displine << 1) + 1][c + (cga->crtc[1] << 4) + 8] = 0;
				}
			} else {
				buffer32->line[(cga->displine << 1)][c] =
				buffer32->line[(cga->displine << 1) + 1][c] = (cga->cgacol & 15) + 16;
				if (cga->cgamode & 1) {
					buffer32->line[(cga->displine << 1)][c + (cga->crtc[1] << 3) + 8] =
					buffer32->line[(cga->displine << 1) + 1][c + (cga->crtc[1] << 3) + 8] = (cga->cgacol & 15) + 16;
				} else {
					buffer32->line[(cga->displine << 1)][c + (cga->crtc[1] << 4) + 8] =
					buffer32->line[(cga->displine << 1) + 1][c + (cga->crtc[1] << 4) + 8] = (cga->cgacol & 15) + 16;
				}
			}
		}
		if (cga->cgamode & 1) {
			for (x = 0; x < cga->crtc[1]; x++) {
				if (cga->cgamode & 8) {
					chr = cga->charbuffer[x << 1];
					attr = cga->charbuffer[(x << 1) + 1];
				} else
					chr = attr = 0;
				drawcursor = ((cga->ma == ca) && cga->con && cga->cursoron);
				cols[1] = (attr & 15) + 16;
				if (cga->cgamode & 0x20) {
					cols[0] = ((attr >> 4) & 7) + 16;
					if ((cga->cgablink & 8) && (attr & 0x80) && !cga->drawcursor)
						cols[1] = cols[0];
				} else
					cols[0] = (attr >> 4) + 16;
				if (drawcursor) {
					for (c = 0; c < 8; c++) {
						buffer32->line[(cga->displine << 1)][(x << 3) + c + 8] =
						buffer32->line[(cga->displine << 1) + 1][(x << 3) + c + 8] =
							cols[(fontdat[chr + cga->fontbase][cga->sc & 7] & (1 << (c ^ 7))) ? 1 : 0] ^ 15;
					}
				} else {
					for (c = 0; c < 8; c++) {
						buffer32->line[(cga->displine << 1)][(x << 3) + c + 8] =
						buffer32->line[(cga->displine << 1) + 1][(x << 3) + c + 8] =
							cols[(fontdat[chr + cga->fontbase][cga->sc & 7] & (1 << (c ^ 7))) ? 1 : 0];
					}
				}
				cga->ma++;
			}
		} else if (!(cga->cgamode & 2)) {
			for (x = 0; x < cga->crtc[1]; x++) {
				if (cga->cgamode & 8) {
					chr  = cga->vram[((cga->ma << 1) & 0xffff)];
					attr = cga->vram[(((cga->ma << 1) + 1) & 0xffff)];
				} else
					chr = attr = 0;
				drawcursor = ((cga->ma == ca) && cga->con && cga->cursoron);
				cols[1] = (attr & 15) + 16;
				if (cga->cgamode & 0x20) {
					cols[0] = ((attr >> 4) & 7) + 16;
					if ((cga->cgablink & 8) && (attr & 0x80))
						cols[1] = cols[0];
				} else
					cols[0] = (attr >> 4) + 16;
				cga->ma++;
				if (drawcursor) {
					for (c = 0; c < 8; c++) {
						buffer32->line[(cga->displine << 1)][(x << 4) + (c << 1) + 8] =
						buffer32->line[(cga->displine << 1)][(x << 4) + (c << 1) + 1 + 8] =
						buffer32->line[(cga->displine << 1) + 1][(x << 4) + (c << 1) + 8] =
						buffer32->line[(cga->displine << 1) + 1][(x << 4) + (c << 1) + 1 + 8] =
							cols[(fontdat[chr + cga->fontbase][cga->sc & 7] & (1 << (c ^ 7))) ? 1 : 0] ^ 15;
					}
				} else {
					for (c = 0; c < 8; c++) {
						buffer32->line[(cga->displine << 1)][(x << 4) + (c << 1) + 8] =
						buffer32->line[(cga->displine << 1)][(x << 4) + (c << 1) + 1 + 8] =
						buffer32->line[(cga->displine << 1) + 1][(x << 4) + (c << 1) + 8] =
						buffer32->line[(cga->displine << 1) + 1][(x << 4) + (c << 1) + 1 + 8] =
						cols[(fontdat[chr + cga->fontbase][cga->sc & 7] & (1 << (c ^ 7))) ? 1 : 0];
					}
				}
			}
		} else if (!(cga->cgamode & 16)) {
			cols[0] = (cga->cgacol & 15) | 16;
			col = (cga->cgacol & 16) ? 24 : 16;
			if (cga->cgamode & 4) {
				cols[1] = col | 3;	/* Cyan */
				cols[2] = col | 4;	/* Red */
				cols[3] = col | 7;	/* White */
			} else if (cga->cgacol & 32) {
				cols[1] = col | 3;	/* Cyan */
				cols[2] = col | 5;	/* Magenta */
				cols[3] = col | 7;	/* White */
			} else {
				cols[1] = col | 2;	/* Green */
				cols[2] = col | 4;	/* Red */
				cols[3] = col | 6;	/* Yellow */
			}
			for (x = 0; x < cga->crtc[1]; x++) {
				if (cga->cgamode & 8)
					dat = (cga->vram[((cga->ma << 1) & 0x1fff) + ((cga->sc & 1) * 0x2000)] << 8) | cga->vram[((cga->ma << 1) & 0x1fff) + ((cga->sc & 1) * 0x2000) + 1];
				else
					dat = 0;
				cga->ma++;
				for (c = 0; c < 8; c++) {
					buffer32->line[(cga->displine << 1)][(x << 4) + (c << 1) + 8] =
					buffer32->line[(cga->displine << 1)][(x << 4) + (c << 1) + 1 + 8] =
					buffer32->line[(cga->displine << 1) + 1][(x << 4) + (c << 1) + 8] =
					buffer32->line[(cga->displine << 1) + 1][(x << 4) + (c << 1) + 1 + 8] =
						cols[dat >> 14];
					dat <<= 2;
				}
			}
		} else {
			cols[0] = 0; cols[1] = (cga->cgacol & 15) + 16;
			for (x = 0; x < cga->crtc[1]; x++) {
				if (cga->cgamode & 8)
					dat = (cga->vram[((cga->ma << 1) & 0x1fff) + ((cga->sc & 1) * 0x2000)] << 8) | cga->vram[((cga->ma << 1) & 0x1fff) + ((cga->sc & 1) * 0x2000) + 1];
				else
					dat = 0;
				cga->ma++;
				for (c = 0; c < 16; c++) {
					buffer32->line[(cga->displine << 1)][(x << 4) + c + 8] =
					buffer32->line[(cga->displine << 1) + 1][(x << 4) + c + 8] =
						cols[dat >> 15];
					dat <<= 1;
				}
			}
		}
	} else {
		cols[0] = ((cga->cgamode & 0x12) == 0x12) ? 0 : (cga->cgacol & 15) + 16;
		if (cga->cgamode & 1) {
			hline(buffer32, 0, (cga->displine << 1), ((cga->crtc[1] << 3) + 16) << 2, cols[0]);
			hline(buffer32, 0, (cga->displine << 1) + 1, ((cga->crtc[1] << 3) + 16) << 2, cols[0]);
		} else {
			hline(buffer32, 0, (cga->displine << 1), ((cga->crtc[1] << 4) + 16) << 2, cols[0]);
			hline(buffer32, 0, (cga->displine << 1) + 1, ((cga->crtc[1] << 4) + 16) << 2, cols[0]);
		}
	}

	if (cga->cgamode & 1)
		x = (cga->crtc[1] << 3) + 16;
	else
		x = (cga->crtc[1] << 4) + 16;

	cga->sc = oldsc;
	if (cga->vc == cga->crtc[7] && !cga->sc)
		cga->cgastat |= 8;
	cga->displine++;
	if (cga->displine >= 360)
		cga->displine = 0;
    } else {
	timer_advance_u64(&cga->timer, cga->dispontime);
	cga->linepos = 0;
	if (cga->vsynctime) {
		cga->vsynctime--;
		if (!cga->vsynctime)
			cga->cgastat &= ~8;
	}
	if (cga->sc == (cga->crtc[11] & 31) || ((cga->crtc[8] & 3) == 3 && cga->sc == ((cga->crtc[11] & 31) >> 1))) {
		cga->con = 0;
		cga->coff = 1;
	}
	if ((cga->crtc[8] & 3) == 3 && cga->sc == (cga->crtc[9] >> 1))
		cga->maback = cga->ma;
	if (cga->vadj) {
		cga->sc++;
		cga->sc &= 31;
		cga->ma = cga->maback;
		cga->vadj--;
		if (!cga->vadj) {
			cga->cgadispon = 1;
			cga->ma = cga->maback = (cga->crtc[13] | (cga->crtc[12] << 8)) & 0xffff;
			cga->sc = 0;
		}
	} else if (cga->sc == cga->crtc[9]) {
		cga->maback = cga->ma;
		cga->sc = 0;
		oldvc = cga->vc;
		cga->vc++;
		cga->vc &= 127;

		if (cga->vc == cga->crtc[6])
			cga->cgadispon = 0;

		if (oldvc == cga->crtc[4]) {
			cga->vc = 0;
			cga->vadj = cga->crtc[5];
			if (!cga->vadj) {
				cga->cgadispon = 1;
				cga->ma = cga->maback = (cga->crtc[13] | (cga->crtc[12] << 8)) & 0xffff;
			}
			switch (cga->crtc[10] & 0x60) {
				case 0x20:
					cga->cursoron = 0;
					break;
				case 0x60:
					cga->cursoron = cga->cgablink & 0x10;
					break;
				default:
					cga->cursoron = cga->cgablink & 0x08;
					break;
			}
		}

		if (cga->vc == cga->crtc[7]) {
			cga->cgadispon = 0;
			cga->displine = 0;
			cga->vsynctime = 16;
			if (cga->crtc[7]) {
				if (cga->cgamode & 1)
					x = (cga->crtc[1] << 3) + 16;
				else
					x = (cga->crtc[1] << 4) + 16;
				cga->lastline++;

				xs_temp = x;
				ys_temp = (cga->lastline - cga->firstline) << 1;

				if ((xs_temp > 0) && (ys_temp > 0)) {
					if (xs_temp < 64) xs_temp = 656;
					if (ys_temp < 32) ys_temp = 400;
					if (!enable_overscan)
						xs_temp -= 16;

					if ((cga->cgamode & 8) && ((xs_temp != xsize) || (ys_temp != ysize) || video_force_resize_get())) {
						xsize = xs_temp;
						ysize = ys_temp;
						set_screen_size(xsize, ysize + (enable_overscan ? 16 : 0));

						if (video_force_resize_get())
							video_force_resize_set(0);
					}

					if (enable_overscan) {
							video_blit_memtoscreen_8(0, (cga->firstline - 4) << 1,
										 xsize, ((cga->lastline - cga->firstline) + 8) << 1);
					} else {
							video_blit_memtoscreen_8(8, cga->firstline << 1,
										 xsize, (cga->lastline - cga->firstline) << 1);
					}
				}

				frames++;

				video_res_x = xsize;
				video_res_y = ysize;
				if (cga->cgamode & 1) {
					video_res_x /= 8;
					video_res_y /= cga->crtc[9] + 1;
					video_bpp = 0;
				} else if (!(cga->cgamode & 2)) {
					video_res_x /= 16;
					video_res_y /= cga->crtc[9] + 1;
					video_bpp = 0;
				} else if (!(cga->cgamode & 16)) {
					video_res_x /= 2;
					video_bpp = 2;
				} else
					video_bpp = 1;
			}
			cga->firstline = 1000;
			cga->lastline = 0;
			cga->cgablink++;
			cga->oddeven ^= 1;
		}
	} else {
		cga->sc++;
		cga->sc &= 31;
		cga->ma = cga->maback;
	}
	if (cga->cgadispon)
		cga->cgastat &= ~1;
	if ((cga->sc == (cga->crtc[10] & 31) || ((cga->crtc[8] & 3) == 3 && cga->sc == ((cga->crtc[10] & 31) >> 1))))
		cga->con = 1;
	if (cga->cgadispon && (cga->cgamode & 1)) {
		for (x = 0; x < (cga->crtc[1] << 1); x++)
			cga->charbuffer[x] = cga->vram[(((cga->ma << 1) + x) & 0xffff)];
	}
    }
}


void
mcga_init(mcga_t *cga)
{
    timer_add(&cga->timer, mcga_poll, cga, 1);
    cga->composite = 0;
}


void *
mcga_standalone_init(const device_t *info)
{
    int display_type;
    mcga_t *cga = malloc(sizeof(mcga_t));

    memset(cga, 0, sizeof(mcga_t));
    video_inform(VIDEO_FLAG_TYPE_CGA, &timing_cga);

    display_type = device_get_config_int("display_type");
    cga->snow_enabled = device_get_config_int("snow_enabled");

    cga->vram = calloc(1, 0x10000);
	cga->mcga_extmode |= 0x80; /* Readable DAC. */

    cga_comp_init(cga->revision);
    timer_add(&cga->timer, mcga_poll, cga, 1);
    mem_mapping_add(&cga->mapping, 0xb8000, 0x08000, mcga_read, NULL, NULL, mcga_write, NULL, NULL, NULL /*cga->vram*/, MEM_MAPPING_EXTERNAL, cga);
    io_sethandler(0x03d0, 0x0010, mcga_in, NULL, NULL, mcga_out, NULL, NULL, cga);

    overscan_x = overscan_y = 16;

    cga->rgb_type = device_get_config_int("rgb_type");
    cga_palette = (cga->rgb_type << 1);
    cgapal_rebuild();

    return cga;
}


void
mcga_close(void *p)
{
    mcga_t *cga = (mcga_t *) p;

    free(cga->vram);
    free(cga);
}


void
mcga_speed_changed(void *p)
{
    mcga_t *cga = (mcga_t *) p;

    mcga_recalctimings(cga);
}

// clang-format off
const device_config_t mcga_config[] = {
    {
        .name = "display_type",
        .description = "Display type",
        .type = CONFIG_SELECTION,
        .default_int = mcga_RGB,
        .selection = {
            {
                .description = "RGB",
                .value = mcga_RGB
            },
            {
                .description = ""
            }
        }
    },
    {
        .name = "rgb_type",
        .description = "RGB type",
        .type = CONFIG_SELECTION,
        .default_int = 0,
        .selection = {
            {
                .description = "Color",
                .value = 0
            },
            {
                .description = "Green Monochrome",
                .value = 1
            },
            {
                .description = "Amber Monochrome",
                .value = 2
            },
            {
                .description = "Gray Monochrome",
                .value = 3
            },
            {
                .description = "Color (no brown)",
                .value = 4
            },
            {
                .description = ""
            }
        }
    },
    {
        .name = "snow_enabled",
        .description = "Snow emulation",
        .type = CONFIG_BINARY,
        .default_int = 1
    },
    {
        .type = CONFIG_END
    }
};
// clang-format on

const device_t mcga_device = {
    .name = "MCGA",
    .internal_name = "mcga",
    .flags = DEVICE_ISA,
    .local = 0,
    .init = mcga_standalone_init,
    .close = mcga_close,
    .reset = NULL,
    { .available = NULL },
    .speed_changed = mcga_speed_changed,
    .force_redraw = NULL,
    .config = mcga_config
};

/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Emulation of the old and new IBM CGA graphics cards.
 *
 *
 *
 * Author:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Copyright 2008-2018 Sarah Walker.
 *		Copyright 2016-2018 Miran Grca.
 */

#ifndef VIDEO_CGA_H
# define VIDEO_CGA_H

#include <stdint.h>
#include <86box/mem.h>
#include <86box/timer.h>
typedef struct mcga_t
{
        mem_mapping_t mapping;

        int crtcreg;
        uint8_t crtc[32];

        uint8_t cgastat;

        uint8_t cgamode, cgacol;

	int fontbase;
        int linepos, displine;
        int sc, vc;
        int cgadispon;
        int con, coff, cursoron, cgablink;
        int vsynctime, vadj;
        uint16_t ma, maback;
        int oddeven;

        uint64_t dispontime, dispofftime;
        pc_timer_t timer;

        int firstline, lastline;

        int drawcursor;

        int fullchange;

        uint8_t *vram;

        uint8_t charbuffer[256];

	int revision;
	int composite;
	int snow_enabled;
	int rgb_type;
} mcga_t;

void    mcga_init(mcga_t *cga);
void    mcga_out(uint16_t addr, uint8_t val, void *p);
uint8_t mcga_in(uint16_t addr, void *p);
void    mcga_write(uint32_t addr, uint8_t val, void *p);
uint8_t mcga_read(uint32_t addr, void *p);
void    mcga_recalctimings(mcga_t *cga);
void    mcga_poll(void *p);

#ifdef EMU_DEVICE_H
extern const device_config_t cga_config[];
extern const device_t cga_device;
#endif

#endif	/*VIDEO_CGA_H*/

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include <stdatomic.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/timer.h>
#include <86box/pci.h>
#include <86box/rom.h>
#include <86box/plat.h>
#include <86box/thread.h>
#include <86box/video.h>
#include <86box/i2c.h>
#include <86box/vid_ddc.h>
#include <86box/vid_svga.h>
#include <86box/vid_svga_render.h>
#include <86box/vid_ati_eeprom.h>

typedef struct r128_t
{
    svga_t svga;
    uint8_t pci_regs[256];

    mem_mapping_t linear_mapping;
    mem_mapping_t reg_base;
    uint32_t io_base;
} r128_t;

static video_timings_t timing_r128_pci = { .type = VIDEO_PCI, .write_b = 2, .write_w = 2, .write_l = 1, .read_b = 20, .read_w = 20, .read_l = 21 };

void
r128_out(uint16_t addr, uint8_t val, void *priv)
{
    r128_t  *vga  = (r128_t *) priv;
    svga_t *svga = &vga->svga;
    uint8_t old;

    if (((addr & 0xfff0) == 0x3d0 || (addr & 0xfff0) == 0x3b0) && !(svga->miscout & 1))
        addr ^= 0x60;

    switch (addr) {
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
r128_in(uint16_t addr, void *priv)
{
    r128_t  *r128  = (r128_t *) priv;
    svga_t *svga = &r128->svga;
    uint8_t temp;

    if (((addr & 0xfff0) == 0x3d0 || (addr & 0xfff0) == 0x3b0) && !(svga->miscout & 1))
        addr ^= 0x60;

    switch (addr) {
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

uint8_t
r128_pci_read(UNUSED(int func), int addr, void *priv)
{
    r128_t* r128 = (r128_t*)priv;

    uint8_t ret = r128->pci_regs[addr & 0xFF];

    switch (addr)
    {
        case 0x00: /* Vendor ID */
            return 0x02;

        case 0x01:
            return 0x10;

        case 0x02: /* Device ID */
            return 0x46;
        
        case 0x03:
            return 0x52;
        
        case 0x08:
            return 0x00;
        
        case 0x09:
            return 0x00;

        case 0x0a:
            return 0x80;
        
        case 0x0b:
            return 0x03;
        
        case 0x0c:
        case 0x0d:
            break;
        
        case 0x0e:
            return 0x00;
        
        case 0x0f:
            return 0x0;

        case 0x14:
            return 0x1;

        case 0x30:
            ret &= 1;
            break;
        
        case 0x31:
            ret = 0x00;
            break;

        case 0x34:
            ret = 0x50;
            break;

        case 0x3d:
            return PCI_INTA;

        case 0x2c ... 0x2f:
            return r128->pci_regs[(addr & 3) + 0x4c];

        case 0x50:
            ret = 0x02;
            break;

        case 0x51:
            ret = 0x5c;
            break;
        
        case 0x52:
            ret = 0x10;
            break;
        
        case 0x54:
            ret = 0x3;
            break;
        
        case 0x55:
            ret = 0x02;
            break;
        
        case 0x57:
            ret = 0x1f;
            break;
        
        case 0x5c:
            ret = 0x1;
            break;
        
        case 0x5d:
            ret = 0x0;
            break;
        
        case 0x5e:
            ret = 0x1;
            break;

        case 0x5f: /* Don't indicate any D1/D2 support at the moment. */
            ret = 0x0;
            break;

        case 0x60:
            ret = 0x0;
            break;

        case 0x63:
            return 0x00;

    }

    return ret;
}

uint8_t
r128_ext_in(uint16_t addr, void *priv)
{
    r128_t* r128 = (r128_t*)priv;
}

void
r128_ext_out(uint16_t addr, uint8_t val, void *priv)
{
    r128_t* r128 = (r128_t*)priv;
}

void r128_pci_write(UNUSED(int func), int addr, uint8_t val, void *priv)
{
    r128_t* r128 = (r128_t*)priv;

    switch (addr & 0xFF)
    {
        case 0x13:
            r128->pci_regs[addr & 0xFF] = val & 0xf8;
            if (r128->pci_regs[PCI_REG_COMMAND] & PCI_COMMAND_MEM)
                mem_mapping_set_addr(&r128->linear_mapping, r128->pci_regs[addr & 0xFF] << 24, (1 << 27) - 1);
            break;
        
        case 0x15:
            io_removehandler(r128->pci_regs[addr & 0xFF], 256, r128_ext_in, NULL, NULL, r128_ext_out, NULL, NULL, r128);
            r128->pci_regs[addr & 0xFF] = val & 0xff;
            if (r128->pci_regs[PCI_REG_COMMAND] & PCI_COMMAND_IO)
                io_sethandler(r128->pci_regs[addr & 0xFF], 256, r128_ext_in, NULL, NULL, r128_ext_out, NULL, NULL, r128);
            break;
        
        case 0x19:
            r128->pci_regs[addr & 0xFF] = val & 0xc0;
            if (r128->pci_regs[PCI_REG_COMMAND] & PCI_COMMAND_MEM)
                mem_mapping_set_addr(&r128->reg_base, (r128->pci_regs[0x19] << 8) | (r128->pci_regs[0x1a] << 16) | (r128->pci_regs[0x1b] << 24), (1 << 14) - 1);
            break;
            
        case 0x04:
            io_removehandler(r128->pci_regs[addr & 0xFF], 256, r128_ext_in, NULL, NULL, r128_ext_out, NULL, NULL, r128);
            mem_mapping_disable(&r128->linear_mapping);
            mem_mapping_disable(&r128->svga.mapping);
            mem_mapping_disable(&r128->reg_base);
            r128->pci_regs[addr & 0xFF] = val & 0xff;
            if (r128->pci_regs[PCI_REG_COMMAND] & PCI_COMMAND_MEM){
                mem_mapping_enable(&r128->linear_mapping);
                mem_mapping_enable(&r128->svga.mapping);
                mem_mapping_enable(&r128->reg_base);
            }
            break;
        
        case 0x3c:
            r128->pci_regs[addr & 0xFF] = val;
            break;
        
        case 0x58:
            r128->pci_regs[addr & 0xFF] = val & 3;
            break;

        case 0x59:
            r128->pci_regs[addr & 0xFF] = val & 3;
            break;

        case 0x5b:
            r128->pci_regs[addr & 0xFF] = val;
            break;

        case 0x4c ... 0x4f:
            r128->pci_regs[addr & 0xFF] = val;
            break;
    }
}
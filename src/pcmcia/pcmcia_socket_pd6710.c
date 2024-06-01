
/* TODO: Dual-socket PD6722. */
#include <stdarg.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/io.h>
#include <86box/timer.h>
#include <86box/pcmcia.h>
#include <86box/mem.h>
#include <86box/pic.h>

#include "cpu.h"

struct pcmcia_socket_pd67xx;

/* We will make the mappings use it as a pointer to avoid cluttering up code. */
typedef struct pd67xx_memory_map
{
    struct pcmcia_socket_pd67xx* main_ptr;
    uint8_t map_num;
    mem_mapping_t mapping;
    union
    {
        uint16_t addr;
        uint8_t addr_b[2];
    } start __attribute__((packed));
    union
    {
        uint16_t addr;
        uint8_t addr_b[2];
    } end __attribute__((packed));
    union
    {
        uint16_t addr;
        uint8_t addr_b[2];
    } offset __attribute__((packed));
} pd67xx_memory_map;

typedef struct pcmcia_socket_pd67xx
{
    uint8_t index;

    uint8_t chip_rev;
    union
    {
        uint8_t interface_status;
        struct {
            uint8_t bvd : 2;
            uint8_t cd : 2;
            uint8_t wp : 1;
            uint8_t ready : 1;
            uint8_t power_on : 1;
            uint8_t vpp_valid : 1;
        };
    };
    uint8_t power_control;
    uint8_t interrupt_general_control;
    uint8_t card_status;
    uint8_t management_interrupt_conf;
    uint8_t mapping_enable;
    uint8_t io_window_control;

    io_range_t ranges[2];
    uint16_t io_offsets[2];

    pd67xx_memory_map mem_maps[5];

    bool inserted;

    pcmcia_socket_t socket;
} pcmcia_socket_pd67xx;

void pd67xx_mem_recalc(pd67xx_memory_map* mem_map)
{
    mem_mapping_disable(&mem_map->mapping);
    if (mem_map->main_ptr->mapping_enable & (1 << mem_map->map_num)) {
        uint32_t start = (mem_map->start.addr & 0x3fff) << 12;
        uint32_t end = (mem_map->end.addr & 0x3fff) << 12;

        if (start < (16 << 12)) {
            start = (16 << 12);
        }

        if (end < start)
            return;
        
        mem_mapping_set_addr(&mem_map->mapping, start, (end - start) + 4096);
    }
}

void pd67xx_mem_recalc_all(pcmcia_socket_pd67xx* pd67xx)
{
    int i = 0;

    for (i = 0; i < 5; i++) {
        pd67xx_mem_recalc(&pd67xx->mem_maps[i]);
    }
}

void pd67xx_mgmt_interrupt(pcmcia_socket_pd67xx* pd67xx, int set)
{
    if ((pd67xx->interrupt_general_control & (1 << 4)) && set) {
        /* Assume that we are asserting I/O Channel Check low. */
        nmi_raise();
    }

    if (set && !!((pd67xx->management_interrupt_conf >> 4) & 0xF))
        picint(1 << (pd67xx->management_interrupt_conf >> 4));
}

void pd67xx_card_interrupt(pcmcia_socket_pd67xx* pd67xx, int set)
{
    if (set && !!(pd67xx->interrupt_general_control & 0xF))
        picint(1 << (pd67xx->interrupt_general_control));
}

void pd67xx_card_inserted(bool inserted, pcmcia_socket_t* socket)
{
    pcmcia_socket_pd67xx* pd67xx = socket->socket_priv;
    bool signal_change = false;

    if (pd67xx->inserted ^ inserted) {
        signal_change = true;
    }

    pd67xx->inserted = inserted;
    pd67xx->cd = inserted ? 0b11 : 0b00;

    if (signal_change && (pd67xx->management_interrupt_conf & (1 << 3))) {
        pd67xx_mgmt_interrupt(pd67xx, 1);
    }
}

uint8_t pd67xx_mem_read(uint32_t addr, void* priv)
{
    pd67xx_memory_map* pd67xx_mem_map = (pd67xx_memory_map*)priv;
    pcmcia_socket_pd67xx* pd67xx = pd67xx_mem_map->main_ptr;

    if (!pd67xx->socket.card_priv)
        return 0xFF;

    if (!pd67xx->socket.mem_read)
        return 0xFF;

    if (!(pd67xx->power_control & (1 << 7))) {
        /* No outputs enabled. */
        return 0xFF;
    }

    addr += (pd67xx_mem_map->offset.addr & 0x3fff) * 4096;
    addr &= 0x3FFFFFF;
    return pd67xx->socket.mem_read(addr, !(pd67xx_mem_map->offset.addr & (1 << 14)), pd67xx->socket.card_priv);
}

void pd67xx_mem_write(uint32_t addr, uint8_t val, void* priv)
{
    pd67xx_memory_map* pd67xx_mem_map = (pd67xx_memory_map*)priv;
    pcmcia_socket_pd67xx* pd67xx = pd67xx_mem_map->main_ptr;

    if (!pd67xx->socket.card_priv)
        return;

    if (!pd67xx->socket.mem_write)
        return;

    if (pd67xx_mem_map->offset.addr & 0x8000) /* write-protected? */
        return;

    if (!(pd67xx->power_control & (1 << 7))) {
        /* No outputs enabled. */
        return;
    }

    addr += (pd67xx_mem_map->offset.addr & 0x3fff) * 4096;
    addr &= 0x3FFFFFF;
    return pd67xx->socket.mem_write(addr, val, !(pd67xx_mem_map->offset.addr & (1 << 14)), pd67xx->socket.card_priv);
}

uint16_t pd67xx_mem_readw(uint32_t addr, void* priv)
{
    pd67xx_memory_map* pd67xx_mem_map = (pd67xx_memory_map*)priv;
    pcmcia_socket_pd67xx* pd67xx = pd67xx_mem_map->main_ptr;

    if (!pd67xx->socket.card_priv)
        return 0xFF;

    if (!pd67xx->socket.mem_readw)
        return 0xFF;

    if (!(pd67xx->power_control & (1 << 7))) {
        /* No outputs enabled. */
        return 0xFFFF;
    }

    addr += (pd67xx_mem_map->offset.addr & 0x3fff) * 4096;
    addr &= 0x3FFFFFF;
    return pd67xx->socket.mem_readw(addr, !(pd67xx_mem_map->offset.addr & (1 << 14)), pd67xx->socket.card_priv);
}

void pd67xx_mem_writew(uint32_t addr, uint16_t val, void* priv)
{
    pd67xx_memory_map* pd67xx_mem_map = (pd67xx_memory_map*)priv;
    pcmcia_socket_pd67xx* pd67xx = pd67xx_mem_map->main_ptr;

    if (!pd67xx->socket.card_priv)
        return;

    if (!pd67xx->socket.mem_writew)
        return;

    if (pd67xx_mem_map->offset.addr & 0x8000) /* write-protected? */
        return;

    if (!(pd67xx->power_control & (1 << 7))) {
        /* No outputs enabled. */
        return;
    }

    addr += (pd67xx_mem_map->offset.addr & 0x3fff) * 4096;
    addr &= 0x3FFFFFF;
    return pd67xx->socket.mem_writew(addr, val, !(pd67xx_mem_map->offset.addr & (1 << 14)), pd67xx->socket.card_priv);
}

uint8_t pd67xx_io_read_1(uint16_t port, void* priv)
{
    pcmcia_socket_pd67xx* pd67xx = priv;

    if (!pd67xx->socket.card_priv)
        return 0xFF;

    if (!pd67xx->socket.io_read)
        return 0xFF;
    
    if (!(pd67xx->power_control & (1 << 7))) {
        /* No outputs enabled. */
        return 0xFF;
    }

    port += pd67xx->io_offsets[0];
    return pd67xx->socket.io_read(port, pd67xx->socket.card_priv);
}

uint16_t pd67xx_io_readw_1(uint16_t port, void* priv)
{
    pcmcia_socket_pd67xx* pd67xx = priv;

    if (!pd67xx->socket.card_priv)
        return 0xFF;

    if (!pd67xx->socket.io_read)
        return 0xFF;

    if (!(pd67xx->power_control & (1 << 7))) {
        /* No outputs enabled. */
        return 0xFFFF;
    }

    port += pd67xx->io_offsets[0];
    return pd67xx->socket.io_readw(port, pd67xx->socket.card_priv);
}

void pd67xx_io_write_1(uint16_t port, uint8_t val, void* priv)
{
    pcmcia_socket_pd67xx* pd67xx = priv;

    if (!pd67xx->socket.card_priv)
        return;

    if (!pd67xx->socket.io_write)
        return;

    if (!(pd67xx->power_control & (1 << 7))) {
        /* No outputs enabled. */
        return;
    }

    port += pd67xx->io_offsets[0];
    return pd67xx->socket.io_write(port, val, pd67xx->socket.card_priv);
}

void pd67xx_io_writew_1(uint16_t port, uint16_t val, void* priv)
{
    pcmcia_socket_pd67xx* pd67xx = priv;

    if (!pd67xx->socket.card_priv)
        return;

    if (!pd67xx->socket.io_writew)
        return;

    if (!(pd67xx->power_control & (1 << 7))) {
        /* No outputs enabled. */
        return;
    }

    port += pd67xx->io_offsets[0];
    return pd67xx->socket.io_write(port, val, pd67xx->socket.card_priv);
}

uint8_t pd67xx_io_read_2(uint16_t port, void* priv)
{
    pcmcia_socket_pd67xx* pd67xx = priv;

    if (!pd67xx->socket.card_priv)
        return 0xFF;

    if (!pd67xx->socket.io_read)
        return 0xFF;

    if (!(pd67xx->power_control & (1 << 7))) {
        /* No outputs enabled. */
        return 0xFF;
    }

    port += pd67xx->io_offsets[1];
    return pd67xx->socket.io_read(port, pd67xx->socket.card_priv);
}

uint16_t pd67xx_io_readw_2(uint16_t port, void* priv)
{
    pcmcia_socket_pd67xx* pd67xx = priv;

    if (!pd67xx->socket.card_priv)
        return 0xFF;

    if (!pd67xx->socket.io_read)
        return 0xFF;

    if (!(pd67xx->power_control & (1 << 7))) {
        /* No outputs enabled. */
        return 0xFFFF;
    }

    port += pd67xx->io_offsets[1];
    return pd67xx->socket.io_readw(port, pd67xx->socket.card_priv);
}

void pd67xx_io_write_2(uint16_t port, uint8_t val, void* priv)
{
    pcmcia_socket_pd67xx* pd67xx = priv;

    if (!pd67xx->socket.card_priv)
        return;

    if (!pd67xx->socket.io_write)
        return;

    if (!(pd67xx->power_control & (1 << 7))) {
        /* No outputs enabled. */
        return;
    }

    port += pd67xx->io_offsets[1];
    return pd67xx->socket.io_write(port, val, pd67xx->socket.card_priv);
}

void pd67xx_io_writew_2(uint16_t port, uint16_t val, void* priv)
{
    pcmcia_socket_pd67xx* pd67xx = priv;

    if (!pd67xx->socket.card_priv)
        return;

    if (!pd67xx->socket.io_writew)
        return;

    if (!(pd67xx->power_control & (1 << 7))) {
        /* No outputs enabled. */
        return;
    }

    port += pd67xx->io_offsets[1];
    return pd67xx->socket.io_write(port, val, pd67xx->socket.card_priv);
}

void pd67xx_port_write(uint16_t port, uint8_t val, void* priv)
{
    pcmcia_socket_pd67xx* pd67xx = priv;

    if (!(port & 1))
        pd67xx->index = val;
    else {
        switch (pd67xx->index) {
            case 0x02:
            {
                pd67xx->power_control = val;
                break;
            }
            case 0x03:
            {
                bool reset = !(val & (1 << 6)) && (pd67xx->interrupt_general_control & (1 << 6));
                pd67xx->interrupt_general_control = val;
                if ((pd67xx->power_control & (1 << 7)) && reset) {
                    pd67xx->socket.reset(pd67xx->socket.card_priv);
                }
                break;
            }
            case 0x05:
            {
                pd67xx->management_interrupt_conf = val;
                break;
            }
            case 0x06:
            {
                pd67xx->mapping_enable = val;
                pd67xx_mem_recalc_all(pd67xx);
                pd67xx->ranges[0].enable = !!(val & (1 << 6));
                pd67xx->ranges[1].enable = !!(val & (1 << 7));
                break;
            }
            case 0x07:
            {
                pd67xx->io_window_control = val;
                break;
            }
            case 0x08:
            {
                pd67xx->ranges[0].start = (pd67xx->ranges[0].start & 0xFF00) | val;
                break;
            }
            case 0x09:
            {
                pd67xx->ranges[0].start = (pd67xx->ranges[0].start & 0xFF) | (val << 8);
                break;
            }
            case 0x0a:
            {
                pd67xx->ranges[0].end = (pd67xx->ranges[0].start & 0xFF00) | val;
                break;
            }
            case 0x0b:
            {
                pd67xx->ranges[0].end = (pd67xx->ranges[0].start & 0xFF) | (val << 8);
                break;
            }
            case 0x0c:
            {
                pd67xx->ranges[1].start = (pd67xx->ranges[1].start & 0xFF00) | val;
                break;
            }
            case 0x0d:
            {
                pd67xx->ranges[1].start = (pd67xx->ranges[1].start & 0xFF) | (val << 8);
                break;
            }
            case 0x0e:
            {
                pd67xx->ranges[1].end = (pd67xx->ranges[1].start & 0xFF00) | val;
                break;
            }
            case 0x0f:
            {
                pd67xx->ranges[1].end = (pd67xx->ranges[1].start & 0xFF) | (val << 8);
                break;
            }
            case 0x10 ... 0x15:
            case 0x18 ... 0x1D:
            case 0x20 ... 0x25:
            case 0x28 ... 0x2D:
            case 0x30 ... 0x35:
            {
                uint8_t mem_map_num = ((pd67xx->index - 0x10) >> 3);
                pd67xx_memory_map *mem_map = &pd67xx->mem_maps[mem_map_num];

                switch (pd67xx->index & 0xF) {
                    case 0: {
                        mem_map->start.addr_b[0] = val;
                        break;
                    }
                    case 1: {
                        mem_map->start.addr_b[1] = val;
                        break;
                    }
                    case 2: {
                        mem_map->end.addr_b[0] = val;
                        break;
                    }
                    case 3: {
                        mem_map->end.addr_b[1] = val;
                        break;
                    }
                    case 4: {
                        mem_map->offset.addr_b[0] = val;
                        break;
                    }
                    case 5: {
                        mem_map->offset.addr_b[1] = val;
                        break;
                    }
                }
                pd67xx_mem_recalc(mem_map);
                break;
            }

            
            case 0x36:
            {
                pd67xx->io_offsets[0] = (pd67xx->io_offsets[0] & 0xFF00) | val;
                break;
            }
            case 0x37:
            {
                pd67xx->io_offsets[0] = (pd67xx->io_offsets[0] & 0xFF) | (val << 8);
                break;
            }
            case 0x38:
            {
                pd67xx->io_offsets[1] = (pd67xx->io_offsets[1] & 0xFF00) | val;
                break;
            }
            case 0x39:
            {
                pd67xx->io_offsets[1] = (pd67xx->io_offsets[1] & 0xFF) | (val << 8);
                break;
            }
        }
    }
}

uint8_t pd67xx_port_read(uint16_t port, void* priv)
{
    pcmcia_socket_pd67xx* pd67xx = priv;

    if (!(port & 1))
        return pd67xx->index;
    else {
        switch (pd67xx->index) {
            case 0x00:
                return 0b10000010;
            case 0x01:
                return pd67xx->interface_status;
            case 0x02:
                return pd67xx->power_control;
            case 0x03:
                return pd67xx->interrupt_general_control;
            case 0x04:
                {
                    uint8_t ret = pd67xx->card_status;
                    pd67xx->card_status = 0;
                    pd67xx_mgmt_interrupt(pd67xx, 0);
                    return ret;
                }
            case 0x05:
                return pd67xx->management_interrupt_conf;
            case 0x06:
                return pd67xx->mapping_enable;
            case 0x07:
            {
                return pd67xx->io_window_control;
            }
            case 0x08:
            {
                return pd67xx->ranges[0].start & 0xFF;
            }
            case 0x09:
            {
                return (pd67xx->ranges[0].start >> 8) & 0xFF;
            }
            case 0x0a:
            {
                return pd67xx->ranges[0].end & 0xFF;
            }
            case 0x0b:
            {
                return (pd67xx->ranges[0].end >> 8) & 0xFF;
            }
            case 0x0c:
            {
                return pd67xx->ranges[1].start & 0xFF;
            }
            case 0x0d:
            {
                return (pd67xx->ranges[1].start >> 8) & 0xFF;
            }
            case 0x0e:
            {
                return pd67xx->ranges[1].end & 0xFF;
            }
            case 0x0f:
            {
                return (pd67xx->ranges[1].end >> 8) & 0xFF;
            }
            
            case 0x10 ... 0x15:
            case 0x18 ... 0x1D:
            case 0x20 ... 0x25:
            case 0x28 ... 0x2D:
            case 0x30 ... 0x35:
            {
                uint8_t mem_map_num = ((pd67xx->index - 0x10) >> 3);
                pd67xx_memory_map *mem_map = &pd67xx->mem_maps[mem_map_num];

                switch (pd67xx->index & 0xF) {
                    case 0: {
                        return mem_map->start.addr_b[0];
                    }
                    case 1: {
                        return mem_map->start.addr_b[1];
                    }
                    case 2: {
                        return mem_map->end.addr_b[0];
                    }
                    case 3: {
                        return mem_map->end.addr_b[1];
                    }
                    case 4: {
                        return mem_map->offset.addr_b[0];
                    }
                    case 5: {
                        return mem_map->offset.addr_b[1];
                    }
                }
                break;
            }
            
            case 0x36:
            case 0x37:
                return (pd67xx->io_offsets[0] >> (8 * (pd67xx->index & 1))) & 0xFF;
            case 0x38:
            case 0x39:
                return (pd67xx->io_offsets[1] >> (8 * (pd67xx->index & 1))) & 0xFF;


            default:
                return 0xFF;
        }
    }
    return 0xFF;
}

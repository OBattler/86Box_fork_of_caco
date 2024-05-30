
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

    pcmcia_socket_t socket;
} pcmcia_socket_pd67xx;

void pd67xx_mem_recalc(pd67xx_memory_map* mem_map)
{
    mem_mapping_disable(&mem_map->mapping);
    if (mem_map->main_ptr->mapping_enable & (1 << mem_map->map_num)) {
        uint32_t start = mem_map->start.addr << 12;
        uint32_t end = (mem_map->end.addr & 0x3fff) << 12;

        if (start < (16 << 12)) {
            start = (16 << 12);
        }

        if (end < start)
            return;
        
        mem_mapping_set_addr(&mem_map->mapping, start, (end - start) + 4096);
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

    port += pd67xx->io_offsets[1];
    return pd67xx->socket.io_write(port, val, pd67xx->socket.card_priv);
}

void pd67xx_port_write(uint16_t port, uint8_t val, void* priv)
{
    pcmcia_socket_pd67xx* pd67xx = priv;

    if (!(port & 1))
        pd67xx->index = val;
    else {

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
            default:
                return 0xFF;
        }
    }
}

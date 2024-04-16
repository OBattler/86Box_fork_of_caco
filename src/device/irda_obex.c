#include "cpu.h"
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/timer.h>
#include <86box/serial.h>
#include <86box/plat.h>
#include <86box/fifo8.h>
#include <86box/irda.h>


typedef struct irda_obex_t
{
    irda_device_t irda;

    uint32_t data_count;
    uint8_t data[0x10000];
    uint8_t in_frame;
    uint8_t complement_next;
} irda_obex_t;

void irda_obex_process_frame(irda_obex_t* irda_obex)
{
    pclog("Received frame of %u bytes\n", irda_obex->data_count);
    if (irda_obex->data_count == 0)
        return;
    pclog("Connection address: 0x%x\n", irda_obex->data[0]);
    pclog("Connection control: 0x%X\n", irda_obex->data[1]);
}

void irda_obex_receive(void *priv, uint8_t data)
{
    irda_obex_t* irda_obex = (irda_obex_t*)priv;

    if (data == 0xc0) {
        irda_obex->in_frame = 1;
        return;
    }
    if (data == 0xc1 && irda_obex->in_frame == 1) {
        irda_obex_process_frame(irda_obex);
        irda_obex->data_count = 0;
        irda_obex->in_frame = 0;
        return;
    }
    if (irda_obex->in_frame == 1 && data == 0x7d) {
        irda_obex->complement_next = 1;
        return;
    }
    if (irda_obex->in_frame == 1) {
        if (irda_obex->complement_next) {
            if (data == 0xc1) {
                /* Discard this frame. */
                irda_obex->data_count = 0;
                return;
            } else {
                irda_obex->data[irda_obex->data_count++] = data ^ (1 << 5);
                return;
            }
        } else {
            irda_obex->data[irda_obex->data_count++] = data;
            return;
        }
    } else if (irda_obex->in_frame == 2) {
        irda_obex->data[irda_obex->data_count++] = data;
        return;
    }
}

void* irda_obex_init(const device_t* info)
{
    irda_obex_t* irda_obex = (irda_obex_t*)calloc(1, sizeof(irda_obex_t));
    irda_obex->irda.write = irda_obex_receive;
    irda_obex->irda.priv = irda_obex;
    irda_register_device(&irda_obex->irda);

    return irda_obex;
}

void irda_obex_close(void* priv)
{
    free(priv);
}

const device_t irda_obex_device = {
    .name          = "IrDA OBEX device",
    .internal_name = "irda_obex",
    .flags         = 0,
    .local         = 0,
    .init          = irda_obex_init,
    .close         = irda_obex_close,
    .reset         = NULL,
    { .poll = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
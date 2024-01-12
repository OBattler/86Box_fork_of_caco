#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/fifo8.h>
#include <86box/timer.h>
#include <86box/usb.h>
#include "cpu.h"
#include <86box/plat_unused.h>

#include "usb_desc.h"

void usb_ep_reset(USBDevice *dev)
{
    int ep;

    dev->ep_ctl.nr = 0;
    dev->ep_ctl.type = USB_ENDPOINT_XFER_CONTROL;
    dev->ep_ctl.ifnum = 0;
    dev->ep_ctl.max_packet_size = 64;
    dev->ep_ctl.max_streams = 0;
    dev->ep_ctl.dev = dev;
    dev->ep_ctl.pipeline = false;
    for (ep = 0; ep < USB_MAX_ENDPOINTS; ep++) {
        dev->ep_in[ep].nr = ep + 1;
        dev->ep_out[ep].nr = ep + 1;
        dev->ep_in[ep].pid = USB_TOKEN_IN;
        dev->ep_out[ep].pid = USB_TOKEN_OUT;
        dev->ep_in[ep].type = USB_ENDPOINT_XFER_INVALID;
        dev->ep_out[ep].type = USB_ENDPOINT_XFER_INVALID;
        dev->ep_in[ep].ifnum = USB_INTERFACE_INVALID;
        dev->ep_out[ep].ifnum = USB_INTERFACE_INVALID;
        dev->ep_in[ep].max_packet_size = 0;
        dev->ep_out[ep].max_packet_size = 0;
        dev->ep_in[ep].max_streams = 0;
        dev->ep_out[ep].max_streams = 0;
        dev->ep_in[ep].dev = dev;
        dev->ep_out[ep].dev = dev;
        dev->ep_in[ep].pipeline = false;
        dev->ep_out[ep].pipeline = false;
    }
}

void usb_ep_init(USBDevice *dev)
{
    int ep;

    usb_ep_reset(dev);
    QTAILQ_INIT(&dev->ep_ctl.queue);
    for (ep = 0; ep < USB_MAX_ENDPOINTS; ep++) {
        QTAILQ_INIT(&dev->ep_in[ep].queue);
        QTAILQ_INIT(&dev->ep_out[ep].queue);
    }
}

void usb_ep_dump(USBDevice *dev)
{
    static const char *tname[] = {
        [USB_ENDPOINT_XFER_CONTROL] = "control",
        [USB_ENDPOINT_XFER_ISOC]    = "isoc",
        [USB_ENDPOINT_XFER_BULK]    = "bulk",
        [USB_ENDPOINT_XFER_INT]     = "int",
    };
    int ifnum, ep, first;

    fprintf(stderr, "Device \"%s\", config %d\n",
            dev->product_desc, dev->configuration);
    for (ifnum = 0; ifnum < 16; ifnum++) {
        first = 1;
        for (ep = 0; ep < USB_MAX_ENDPOINTS; ep++) {
            if (dev->ep_in[ep].type != USB_ENDPOINT_XFER_INVALID &&
                dev->ep_in[ep].ifnum == ifnum) {
                if (first) {
                    first = 0;
                    fprintf(stderr, "  Interface %d, alternative %d\n",
                            ifnum, dev->altsetting[ifnum]);
                }
                fprintf(stderr, "    Endpoint %d, IN, %s, %d max\n", ep,
                        tname[dev->ep_in[ep].type],
                        dev->ep_in[ep].max_packet_size);
            }
            if (dev->ep_out[ep].type != USB_ENDPOINT_XFER_INVALID &&
                dev->ep_out[ep].ifnum == ifnum) {
                if (first) {
                    first = 0;
                    fprintf(stderr, "  Interface %d, alternative %d\n",
                            ifnum, dev->altsetting[ifnum]);
                }
                fprintf(stderr, "    Endpoint %d, OUT, %s, %d max\n", ep,
                        tname[dev->ep_out[ep].type],
                        dev->ep_out[ep].max_packet_size);
            }
        }
    }
    fprintf(stderr, "--\n");
}

struct USBEndpoint *usb_ep_get(USBDevice *dev, int pid, int ep)
{
    struct USBEndpoint *eps;

    assert(dev != NULL);
    if (ep == 0) {
        return &dev->ep_ctl;
    }
    assert(pid == USB_TOKEN_IN || pid == USB_TOKEN_OUT);
    assert(ep > 0 && ep <= USB_MAX_ENDPOINTS);
    eps = (pid == USB_TOKEN_IN) ? dev->ep_in : dev->ep_out;
    return eps + ep - 1;
}

uint8_t usb_ep_get_type(USBDevice *dev, int pid, int ep)
{
    struct USBEndpoint *uep = usb_ep_get(dev, pid, ep);
    return uep->type;
}

void usb_ep_set_type(USBDevice *dev, int pid, int ep, uint8_t type)
{
    struct USBEndpoint *uep = usb_ep_get(dev, pid, ep);
    uep->type = type;
}

void usb_ep_set_ifnum(USBDevice *dev, int pid, int ep, uint8_t ifnum)
{
    struct USBEndpoint *uep = usb_ep_get(dev, pid, ep);
    uep->ifnum = ifnum;
}

void usb_ep_set_max_packet_size(USBDevice *dev, int pid, int ep,
                                uint16_t raw)
{
    struct USBEndpoint *uep = usb_ep_get(dev, pid, ep);
    int size, microframes;

    size = raw & 0x7ff;
    switch ((raw >> 11) & 3) {
    case 1:
        microframes = 2;
        break;
    case 2:
        microframes = 3;
        break;
    default:
        microframes = 1;
        break;
    }
    uep->max_packet_size = size * microframes;
}

void usb_ep_set_max_streams(USBDevice *dev, int pid, int ep, uint8_t raw)
{
    struct USBEndpoint *uep = usb_ep_get(dev, pid, ep);
    int MaxStreams;

    MaxStreams = raw & 0x1f;
    if (MaxStreams) {
        uep->max_streams = 1 << MaxStreams;
    } else {
        uep->max_streams = 0;
    }
}

void usb_ep_set_halted(USBDevice *dev, int pid, int ep, bool halted)
{
    struct USBEndpoint *uep = usb_ep_get(dev, pid, ep);
    uep->halted = halted;
}

USBPacket *usb_ep_find_packet_by_id(USBDevice *dev, int pid, int ep,
                                    uint64_t id)
{
    struct USBEndpoint *uep = usb_ep_get(dev, pid, ep);
    USBPacket *p;

    QTAILQ_FOREACH(p, &uep->queue, queue) {
        if (p->id == id) {
            return p;
        }
    }

    return NULL;
}

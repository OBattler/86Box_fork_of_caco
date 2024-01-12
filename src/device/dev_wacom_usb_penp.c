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
#include <86box/plat.h>
#include <86box/mouse.h>
#include "cpu.h"
#include <86box/plat_unused.h>

#include "../usb/usb_desc.h"

/* Interface requests */
#define WACOM_GET_REPORT    0x2101
#define WACOM_SET_REPORT    0x2109

struct USBWacomState {
    USBDevice dev;
    USBEndpoint *intr;
    int dx, dy, dz, buttons_state;
    int x, y;
    int mouse_grabbed;
    enum {
        WACOM_MODE_HID = 1,
        WACOM_MODE_WACOM = 2,
    } mode;
    uint8_t idle;
    int changed;
};

typedef struct USBWacomState USBWacomState;

enum {
    STR_MANUFACTURER = 1,
    STR_PRODUCT,
    STR_SERIALNUMBER,
};

static const USBDescStrings desc_strings = {
    [STR_MANUFACTURER]     = "QEMU",
    [STR_PRODUCT]          = "Wacom PenPartner",
    [STR_SERIALNUMBER]     = "1",
};

static const uint8_t qemu_wacom_hid_report_descriptor[] = {
    0x05, 0x01,      /* Usage Page (Desktop) */
    0x09, 0x02,      /* Usage (Mouse) */
    0xa1, 0x01,      /* Collection (Application) */
    0x85, 0x01,      /*    Report ID (1) */
    0x09, 0x01,      /*    Usage (Pointer) */
    0xa1, 0x00,      /*    Collection (Physical) */
    0x05, 0x09,      /*       Usage Page (Button) */
    0x19, 0x01,      /*       Usage Minimum (01h) */
    0x29, 0x03,      /*       Usage Maximum (03h) */
    0x15, 0x00,      /*       Logical Minimum (0) */
    0x25, 0x01,      /*       Logical Maximum (1) */
    0x95, 0x03,      /*       Report Count (3) */
    0x75, 0x01,      /*       Report Size (1) */
    0x81, 0x02,      /*       Input (Data, Variable, Absolute) */
    0x95, 0x01,      /*       Report Count (1) */
    0x75, 0x05,      /*       Report Size (5) */
    0x81, 0x01,      /*       Input (Constant) */
    0x05, 0x01,      /*       Usage Page (Desktop) */
    0x09, 0x30,      /*       Usage (X) */
    0x09, 0x31,      /*       Usage (Y) */
    0x09, 0x38,      /*       Usage (Wheel) */
    0x15, 0x81,      /*       Logical Minimum (-127) */
    0x25, 0x7f,      /*       Logical Maximum (127) */
    0x75, 0x08,      /*       Report Size (8) */
    0x95, 0x03,      /*       Report Count (3) */
    0x81, 0x06,      /*       Input (Data, Variable, Relative) */
    0x95, 0x03,      /*       Report Count (3) */
    0x81, 0x01,      /*       Input (Constant) */
    0xc0,            /*    End Collection */
    0xc0,            /* End Collection */
    0x05, 0x0d,      /* Usage Page (Digitizer) */
    0x09, 0x01,      /* Usage (Digitizer) */
    0xa1, 0x01,      /* Collection (Application) */
    0x85, 0x02,      /*    Report ID (2) */
    0xa1, 0x00,      /*    Collection (Physical) */
    0x06, 0x00, 0xff,/*       Usage Page (ff00h), vendor-defined */
    0x09, 0x01,      /*       Usage (01h) */
    0x15, 0x00,      /*       Logical Minimum (0) */
    0x26, 0xff, 0x00,/*       Logical Maximum (255) */
    0x75, 0x08,      /*       Report Size (8) */
    0x95, 0x07,      /*       Report Count (7) */
    0x81, 0x02,      /*       Input (Data, Variable, Absolute) */
    0xc0,            /*    End Collection */
    0x09, 0x01,      /*    Usage (01h) */
    0x85, 0x63,      /*    Report ID (99) */
    0x95, 0x07,      /*    Report Count (7) */
    0x81, 0x02,      /*    Input (Data, Variable, Absolute) */
    0x09, 0x01,      /*    Usage (01h) */
    0x85, 0x02,      /*    Report ID (2) */
    0x95, 0x01,      /*    Report Count (1) */
    0xb1, 0x02,      /*    Feature (Variable) */
    0x09, 0x01,      /*    Usage (01h) */
    0x85, 0x03,      /*    Report ID (3) */
    0x95, 0x01,      /*    Report Count (1) */
    0xb1, 0x02,      /*    Feature (Variable) */
    0xc0             /* End Collection */
};

static const USBDescIface desc_iface_wacom = {
    .bInterfaceNumber              = 0,
    .bNumEndpoints                 = 1,
    .bInterfaceClass               = USB_CLASS_HID,
    .bInterfaceSubClass            = 0x01, /* boot */
    .bInterfaceProtocol            = 0x02,
    .ndesc                         = 1,
    .descs = (USBDescOther[]) {
        {
            /* HID descriptor */
            .data = (uint8_t[]) {
                0x09,          /*  u8  bLength */
                0x21,          /*  u8  bDescriptorType */
                0x01, 0x10,    /*  u16 HID_class */
                0x00,          /*  u8  country_code */
                0x01,          /*  u8  num_descriptors */
                0x22,          /*  u8  type: Report */
                sizeof(qemu_wacom_hid_report_descriptor), 0, /*  u16 len */
            },
        },
    },
    .eps = (USBDescEndpoint[]) {
        {
            .bEndpointAddress      = USB_DIR_IN | 0x01,
            .bmAttributes          = USB_ENDPOINT_XFER_INT,
            .wMaxPacketSize        = 8,
            .bInterval             = 0x0a,
        },
    },
};

static const USBDescDevice desc_device_wacom = {
    .bcdUSB                        = 0x0110,
    .bMaxPacketSize0               = 8,
    .bNumConfigurations            = 1,
    .confs = (USBDescConfig[]) {
        {
            .bNumInterfaces        = 1,
            .bConfigurationValue   = 1,
            .bmAttributes          = USB_CFG_ATT_ONE,
            .bMaxPower             = 40,
            .nif = 1,
            .ifs = &desc_iface_wacom,
        },
    },
};

static const USBDesc desc_wacom = {
    .id = {
        .idVendor          = 0x056a,
        .idProduct         = 0x0000,
        .bcdDevice         = 0x4210,
        .iManufacturer     = STR_MANUFACTURER,
        .iProduct          = STR_PRODUCT,
        .iSerialNumber     = STR_SERIALNUMBER,
    },
    .full = &desc_device_wacom,
    .str  = desc_strings,
};

static void usb_mouse_event(void *opaque,
                            int dx1, int dy1, int dz1, int buttons_state)
{
    USBWacomState *s = opaque;

    s->dx += dx1;
    s->dy += dy1;
    s->dz += dz1;
    s->buttons_state = buttons_state;
    s->changed = 1;
    //usb_wakeup(s->intr, 0);
}

static void usb_wacom_event(void *opaque,
                            int x, int y, int dz, int buttons_state)
{
    USBWacomState *s = opaque;

    /* scale to Penpartner resolution */
    s->x = (x * 5040 / 0x7FFF);
    s->y = (y * 3780 / 0x7FFF);
    s->dz += dz;
    s->buttons_state = buttons_state;
    s->changed = 1;
    //usb_wakeup(s->intr, 0);
}

static inline int int_clamp(int val, int vmin, int vmax)
{
    if (val < vmin)
        return vmin;
    else if (val > vmax)
        return vmax;
    else
        return val;
}

static int usb_mouse_poll(USBWacomState *s, uint8_t *buf, int len)
{
    int dx, dy, dz, b, l;


    dx = int_clamp(s->dx, -128, 127);
    dy = int_clamp(s->dy, -128, 127);
    dz = int_clamp(s->dz, -128, 127);

    s->dx -= dx;
    s->dy -= dy;
    s->dz -= dz;

    b = 0;
    if (s->buttons_state & 1)
        b |= 0x01;
    if (s->buttons_state & 2)
        b |= 0x02;
    if (s->buttons_state & 4)
        b |= 0x04;

    buf[0] = b;
    buf[1] = dx;
    buf[2] = dy;
    l = 3;
    if (len >= 4) {
        buf[3] = dz;
        l = 4;
    }
    return l;
}

static int usb_wacom_poll(USBWacomState *s, uint8_t *buf, int len)
{
    int b;

    b = 0;
    if (s->buttons_state & 1)
        b |= 0x01;
    if (s->buttons_state & 2)
        b |= 0x40;
    if (s->buttons_state & 4)
        b |= 0x20; /* eraser */

    if (len < 7)
        return 0;

    buf[0] = s->mode;
    buf[5] = 0x00 | (b & 0xf0);
    buf[1] = s->x & 0xff;
    buf[2] = s->x >> 8;
    buf[3] = s->y & 0xff;
    buf[4] = s->y >> 8;
    if (b & 0x3f) {
        buf[6] = 0;
    } else {
        buf[6] = (unsigned char) -127;
    }

    return 7;
}

static void usb_wacom_handle_reset(USBDevice *dev)
{
    USBWacomState *s = (USBWacomState *) dev->opaque;

    s->dx = 0;
    s->dy = 0;
    s->dz = 0;
    s->x = 0;
    s->y = 0;
    s->buttons_state = 0;
    s->mode = WACOM_MODE_HID;
}

static void usb_wacom_handle_control(USBDevice *dev, USBPacket *p,
               int request, int value, int index, int length, uint8_t *data)
{
    USBWacomState *s = (USBWacomState *) dev->opaque;
    int ret;

    ret = usb_desc_handle_control(dev, p, request, value, index, length, data);
    if (ret >= 0) {
        return;
    }

    switch (request) {
    case InterfaceRequest | USB_REQ_GET_DESCRIPTOR:
        switch (value >> 8) {
            case 0x22:
                    memcpy(data, qemu_wacom_hid_report_descriptor,
                        sizeof(qemu_wacom_hid_report_descriptor));
                    p->actual_length = sizeof(qemu_wacom_hid_report_descriptor);
                break;
            default:
                return;
        }
        break;
        case WACOM_SET_REPORT:
            s->mode = data[0];
            break;
        case WACOM_GET_REPORT:
            data[0] = 0;
            data[1] = s->mode;
            p->actual_length = 2;
            break;
        /* USB HID requests */
        case 0xa101: /* HID_GET_REPORT */
            if (s->mode == WACOM_MODE_HID)
                p->actual_length = usb_mouse_poll(s, data, length);
            else if (s->mode == WACOM_MODE_WACOM)
                p->actual_length = usb_wacom_poll(s, data, length);
            break;
        case 0xa102: /* HID_GET_IDLE */
            data[0] = s->idle;
            p->actual_length = 1;
            break;
        case 0x210a: /* HID_SET_IDLE */
            s->idle = (uint8_t) (value >> 8);
            break;
        default:
            p->status = USB_RET_STALL;
            break;

    }
}


static void usb_wacom_handle_data(USBDevice *dev, USBPacket *p)
{
    USBWacomState *s = (USBWacomState *) dev->opaque;
    uint8_t *buf = malloc(p->iov.size);
    int len = 0;

    switch (p->pid) {
    case USB_TOKEN_IN:
        if (p->ep->nr == 1) {
            if (!(s->changed || s->idle)) {
                p->status = USB_RET_NAK;
                return;
            }
            s->changed = 0;
            if (s->mode == WACOM_MODE_HID)
                len = usb_mouse_poll(s, buf, p->iov.size);
            else if (s->mode == WACOM_MODE_WACOM)
                len = usb_wacom_poll(s, buf, p->iov.size);
            usb_packet_copy(p, buf, len);
            break;
        }
        /* Fall through.  */
    case USB_TOKEN_OUT:
    default:
        p->status = USB_RET_STALL;
    }
    free(buf);
}

static int
usb_mouse_poll_emu(void *priv)
{
    USBDevice *dev = (USBDevice*)priv;
    USBWacomState *s = (USBWacomState *)dev->opaque;

    s->dx = s->dy = 0;
    mouse_subtract_coords(&s->dx, &s->dy, NULL, NULL, -128, 127, 0, 0);
    //mouse_subtract_z(&usb_mouse->dz, -8, 7, 1);
    s->buttons_state = mouse_get_buttons_ex();
    s->changed = 1;

    return (!mouse_capture && !video_fullscreen) || (!mouse_state_changed());
}

void*
usb_wacom_init(const device_t* info)
{
    USBDevice *dev = (USBDevice*) calloc(1, sizeof(USBDevice));
    USBWacomState *s = (USBWacomState*)calloc(1, sizeof(USBWacomState));
    usb_device_init(dev, "QEMU PenPartner Tablet");
    dev->usb_desc       = &desc_wacom;
    dev->handle_reset   = usb_wacom_handle_reset;
    dev->handle_control = usb_wacom_handle_control;
    dev->handle_data    = usb_wacom_handle_data;
    dev->serial         = strdup("test");
    dev->priv           = s;

    usb_desc_create_serial(dev);
    usb_desc_init(dev);
    s->intr = usb_ep_get(dev, USB_TOKEN_IN, 1);
    s->changed = 1;
    
    dev->port = usb_attach_device(dev, USB_BUS_UHCI);
    if (!dev->port) {
        pclog("No available USB ports\n");
        free(dev->priv);
        free(dev);
        return NULL;
    }
    mouse_set_buttons(4);
    mouse_set_sample_rate(0);
    usb_wacom_handle_reset(dev);
    return dev;
}

void
usb_wacom_close(void* priv)
{
    USBDevice *dev = (USBDevice*) priv;
    usb_ep_reset(dev);
    free(dev->opaque);
    free(dev);
}

const device_t wacom_penp_usb_device = {
    .name          = "QEMU Wacom PenPartner",
    .internal_name = "usb_wacom",
    .flags         = DEVICE_USB,
    .local         = 0,
    .init          = usb_wacom_init,
    .close         = usb_wacom_close,
    .reset         = NULL,
    { .poll = usb_mouse_poll_emu },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Universal Host Controller Interface implementation.
 *
 *
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *          Cacodemon345
 * 
 *          Copyright 2020 Miran Grca.
 *          Copyright 2023 Cacodemon345.
 */

/* Ported over from QEMU. */
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <wchar.h>
#include <assert.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/timer.h>
#include <86box/fifo8.h>
#include <86box/usb.h>
#include <86box/dma.h>

enum uhci_regs_enum
{
    UHCI_USBCMD = 0x00,
    UHCI_USBSTS = 0x02,
    UHCI_USBINTR = 0x04,
    UHCI_FRNUM = 0x06,
    UHCI_FLBASEADD = 0x08,
    UHCI_FLBASEADD_2 = 0x0A,
    UHCI_SOFMOD = 0x0C,
    UHCI_PORTSC_1 = 0x10,
    UHCI_PORTSC_2 = 0x12,
};

#define UHCI_STS_HCHALTED (1 << 5)
#define UHCI_STS_HCPERR   (1 << 4)
#define UHCI_STS_HSERR    (1 << 3)
#define UHCI_STS_RD       (1 << 2)
#define UHCI_STS_USBERR   (1 << 1)
#define UHCI_STS_USBINT   (1 << 0)

#define UHCI_PORT_SUSPEND (1 << 12)
#define UHCI_PORT_RESET (1 << 9)
#define UHCI_PORT_LSDA  (1 << 8)
#define UHCI_PORT_RSVD1 (1 << 7)
#define UHCI_PORT_RD    (1 << 6)
#define UHCI_PORT_ENC   (1 << 3)
#define UHCI_PORT_EN    (1 << 2)
#define UHCI_PORT_CSC   (1 << 1)
#define UHCI_PORT_CCS   (1 << 0)

#define UHCI_CMD_FGR      (1 << 4)
#define UHCI_CMD_EGSM     (1 << 3)
#define UHCI_CMD_GRESET   (1 << 2)
#define UHCI_CMD_HCRESET  (1 << 1)
#define UHCI_CMD_RS       (1 << 0)


static void uhci_update_irq(usb_t* dev);

void uhci_process_frame_schedule(void* p);

void uhci_reset(usb_t* dev)
{
    pclog("UHCI: Reset\n");
    memset(dev->uhci_io, 0x00, sizeof(dev->uhci_io));
    dev->uhci_io[0x0c] = 0x40;
    dev->uhci_io[0x10] = dev->uhci_io[0x12] = 0x80;
    
    if (dev->uhci_devices[0]) {
        usb_device_t* usbdev = dev->uhci_devices[0];
        usb_detach_device(dev, 0 | (USB_BUS_UHCI << 8));
        usb_attach_device(dev, usbdev, USB_BUS_UHCI);
        dev->uhci_devices[0]->device_reset(dev->uhci_devices[0]->priv);
    }
    if (dev->uhci_devices[1]) {
        usb_device_t* usbdev = dev->uhci_devices[1];
        usb_detach_device(dev, 1 | (USB_BUS_UHCI << 8));
        usb_attach_device(dev, usbdev, USB_BUS_UHCI);
        dev->uhci_devices[1]->device_reset(dev->uhci_devices[0]->priv);
    }
    timer_disable(&dev->uhci_frame_timer);
}

/* signal resume if controller suspended */
static void uhci_resume(usb_t *dev)
{
    if (dev->uhci_io_regs.usbcmd & UHCI_CMD_EGSM) {
        dev->uhci_io_regs.usbcmd |= UHCI_CMD_FGR;
        dev->uhci_io_regs.status |= UHCI_STS_RD;
        uhci_update_irq(dev);
    }
}

uint16_t
uhci_attach_device(usb_t *dev, usb_device_t* device)
{
    uint16_t *ports = (uint16_t *) &dev->uhci_io[0x10];
    uint32_t i = 0;

    for (i = 0; i < 2; i++) {
        if (!dev->uhci_devices[i]) {
            dev->uhci_devices[i] = device;
            ports[i] |= UHCI_PORT_CCS | UHCI_PORT_CSC;
            /* All attached devices are assumed to be full-speed by default. */
            ports[i] &= ~UHCI_PORT_LSDA;
            
            uhci_resume(dev);
            return (i) | (USB_BUS_UHCI << 8);
        }
    }
    return 0xFFFF;
}

void
uhci_detach_device(usb_t *dev, uint16_t port)
{
    uint16_t *ports = (uint16_t *) &dev->uhci_io[0x10];

    port &= 0xFF;
    if (port > 2)
        return;
    if (dev->uhci_devices[port]) {
        dev->uhci_devices[port] = NULL;
        if (ports[port] & UHCI_PORT_CCS) {
            ports[port] &= ~UHCI_PORT_CCS;
            ports[port] |= UHCI_PORT_CSC;
        }
        if (ports[port] & UHCI_PORT_EN) {
            ports[port] &= ~UHCI_PORT_EN;
            ports[port] |= UHCI_PORT_ENC;
        }
        uhci_resume(dev);
    }
}

uint8_t uhci_reg_read(uint16_t addr, void *p)
{
    usb_t   *dev = (usb_t *) p;
    uint8_t  ret;
    uint8_t *regs = dev->uhci_io;

    addr &= 0x0000001f;

    ret = regs[addr];
    if (addr >= 0x14) {
        ret = (ret & 1) ? 0xff : 0x7f;
    }

    return ret;
}

#define UHCI_PORT_READ_ONLY    (0x1bb)
#define UHCI_PORT_WRITE_CLEAR  (UHCI_PORT_CSC | UHCI_PORT_ENC)

void
uhci_reg_write(uint16_t addr, uint8_t val, void *p)
{
    usb_t   *dev  = (usb_t *) p;
    uint8_t *regs = dev->uhci_io;

    addr &= 0x0000001f;

    switch (addr) {
        case UHCI_USBSTS:
            regs[UHCI_USBSTS] &= ~(val & 0x3f);
            if (val & UHCI_STS_USBINT)
                dev->uhci_status_shadow = 0x00;
            uhci_update_irq(dev);
            break;
        case UHCI_USBINTR:
            regs[UHCI_USBINTR] = (val & 0x0f);
            uhci_update_irq(dev);
            break;
        case UHCI_FLBASEADD + 1:
            regs[UHCI_FLBASEADD + 1] = (val & 0xf0);
            break;
        case UHCI_FLBASEADD_2:
        case UHCI_FLBASEADD_2 + 1:
            regs[addr] = val;
            break;
        case UHCI_SOFMOD:
            regs[UHCI_SOFMOD] = (val & 0x7f);
            break;
    }
}

void
uhci_reg_writew(uint16_t addr, uint16_t val, void *p)
{
    usb_t    *dev  = (usb_t *) p;
    uint16_t *regs = (uint16_t *) dev->uhci_io;
    uint16_t old = regs[addr >> 1];

    addr &= 0x0000001f;

    switch (addr) {
        case UHCI_USBCMD:
            if ((val & UHCI_CMD_RS) && !(regs[UHCI_USBCMD >> 1] & UHCI_CMD_RS)){
                regs[UHCI_USBSTS >> 1] &= ~UHCI_STS_HCHALTED;
                timer_on_auto(&dev->uhci_frame_timer, 1000.);
            }
            else if (!(val & UHCI_CMD_RS)){
                regs[UHCI_USBSTS >> 1] |= UHCI_STS_HCHALTED;
            }
            if (val & (UHCI_CMD_GRESET | UHCI_CMD_HCRESET)) {
                uhci_reset(dev);
                return;
            }
            regs[addr >> 1] = (val & 0x00ff);
            if (val & UHCI_CMD_EGSM) {
                if ((regs[UHCI_PORTSC_1 >> 1] & UHCI_PORT_RD) || (regs[UHCI_PORTSC_2 >> 1] & UHCI_PORT_RD)) {
                    uhci_resume(dev);
                }
            }
            break;
        case UHCI_FRNUM:
            if (!(regs[UHCI_USBCMD >> 1] & UHCI_CMD_RS))
                regs[addr >> 1] = (val & 0x07ff);
            break;
        case UHCI_PORTSC_1:
        case UHCI_PORTSC_2:
        {
            uint16_t *port = &regs[addr >> 1];

            if (dev->uhci_devices[(addr - UHCI_PORTSC_1) >> 1]) {
                if ((val & UHCI_PORT_RESET) && !(old & UHCI_PORT_RESET)) {
                    dev->uhci_devices[(addr - UHCI_PORTSC_1) >> 1]->device_reset(dev->uhci_devices[(addr - UHCI_PORTSC_1) >> 1]->priv);
                }
            }

            *port &= UHCI_PORT_READ_ONLY;
            /* enabled may only be set if a device is connected */
            if (!(*port & UHCI_PORT_CCS)) {
                val &= ~UHCI_PORT_EN;
            }
            *port |= (val & ~UHCI_PORT_READ_ONLY);
            /* some bits are reset when a '1' is written to them */
            *port &= ~(val & UHCI_PORT_WRITE_CLEAR);

            break;
        }
        default:
            uhci_reg_write(addr, val & 0xff, p);
            uhci_reg_write(addr + 1, (val >> 8) & 0xff, p);
            break;
    }
}

static void uhci_update_irq(usb_t *dev)
{
    int level = 0;
    if (((dev->uhci_status_shadow & 1) && (dev->uhci_io_regs.intr & (1 << 2))) ||
        ((dev->uhci_status_shadow & 2) && (dev->uhci_io_regs.intr & (1 << 3))) ||
        ((dev->uhci_io_regs.status & UHCI_STS_USBERR) && (dev->uhci_io_regs.intr & (1 << 0))) ||
        ((dev->uhci_io_regs.status & UHCI_STS_RD) && (dev->uhci_io_regs.intr & (1 << 1))) ||
        (dev->uhci_io_regs.status & UHCI_STS_HSERR) ||
        (dev->uhci_io_regs.status & UHCI_STS_HCPERR)) {
        level = 1;
    }
    /* No need for smi_handle here. */
    if (dev->uhci_irq_level != level) {
        dev->uhci_irq_level = level;
        if (dev->usb_params && dev->usb_params->parent_priv && dev->usb_params->update_interrupt) {
            dev->usb_params->update_interrupt(dev, dev->usb_params->parent_priv);
        }
    }
}

typedef struct UHCI_TD {
    uint32_t link;
    uint32_t ctrl;
    uint32_t token;
    uint32_t buffer;
} UHCI_TD;

typedef struct UHCI_QH {
    uint32_t link;
    uint32_t el_link;
} UHCI_QH;

/* QH DB used for detecting QH loops */
#define UHCI_MAX_QUEUES 128
typedef struct {
    uint32_t addr[UHCI_MAX_QUEUES];
    uint32_t count;
} QhDb;

static void qhdb_reset(QhDb *db)
{
    db->count = 0;
}

/* Add QH to DB. Returns 1 if already present or DB is full. */
static int qhdb_insert(QhDb *db, uint32_t addr)
{
    int i;
    if (db->count >= UHCI_MAX_QUEUES)
        return 1;

    for (i = 0; i < db->count; i++)
        if (db->addr[i] == addr)
            return 1;

    db->addr[db->count++] = addr;
    return 0;
}

enum {
    TD_RESULT_STOP_FRAME = 10,
    TD_RESULT_COMPLETE,
    TD_RESULT_NEXT_QH,
};

#define TD_CTRL_SPD     (1 << 29)
#define TD_CTRL_ERROR_SHIFT  27
#define TD_CTRL_IOS     (1 << 25)
#define TD_CTRL_IOC     (1 << 24)
#define TD_CTRL_ACTIVE  (1 << 23)
#define TD_CTRL_STALL   (1 << 22)
#define TD_CTRL_BABBLE  (1 << 20)
#define TD_CTRL_NAK     (1 << 19)
#define TD_CTRL_TIMEOUT (1 << 18)

static __inline int is_valid(uint32_t link)
{
    return (link & 1) == 0;
}

static __inline int is_qh(uint32_t link)
{
    return (link & 2) != 0;
}

static __inline int depth_first(uint32_t link)
{
    return (link & 4) != 0;
}

#define FRAME_MAX_LOOPS  256

/* Must be large enough to handle 10 frame delay for initial isoc requests */
#define QH_VALID         32

#define MAX_FRAMES_PER_TICK    (QH_VALID / 2)

static int uhci_handle_td_error(usb_t *dev, UHCI_TD *td, uint32_t td_addr,
                                int status, uint32_t *int_mask)
{
    int ret;

    pclog("UHCI: TD error\n");

    switch (status) {
    case USB_ERROR_NAK:
        td->ctrl |= TD_CTRL_NAK;
        return TD_RESULT_NEXT_QH;

    case USB_ERROR_STALL:
        td->ctrl |= TD_CTRL_STALL;
        ret = TD_RESULT_NEXT_QH;
        break;

    case USB_ERROR_OVERRUN:
        td->ctrl |= TD_CTRL_BABBLE | TD_CTRL_STALL;
        /* frame interrupted */
        ret = TD_RESULT_STOP_FRAME;
        break;

    case USB_ERROR_NODEV:
    default:
        td->ctrl |= TD_CTRL_TIMEOUT;
        td->ctrl &= ~(3 << TD_CTRL_ERROR_SHIFT);
        ret = TD_RESULT_NEXT_QH;
        break;
    }

    td->ctrl &= ~TD_CTRL_ACTIVE;
    dev->uhci_io_regs.status |= UHCI_STS_USBERR;
    if (td->ctrl & TD_CTRL_IOC) {
        *int_mask |= 0x01;
    }
    uhci_update_irq(dev);
    return ret;
}

static int uhci_complete_td(usb_t *dev, UHCI_TD *td, uint32_t td_addr, int status, uint32_t actual_length, uint8_t* buf, uint32_t *int_mask)
{
    int len = 0, max_len;
    uint8_t pid;

    max_len = ((td->token >> 21) + 1) & 0x7ff;
    pid = td->token & 0xff;

    if (td->ctrl & TD_CTRL_IOS)
        td->ctrl &= ~TD_CTRL_ACTIVE;

    if (status != USB_ERROR_NO_ERROR) {
        return uhci_handle_td_error(dev, td, td_addr,
                                    status, int_mask);
    }

    len = actual_length;
    td->ctrl = (td->ctrl & ~0x7ff) | ((len - 1) & 0x7ff);

    /* The NAK bit may have been set by a previous frame, so clear it
       here.  The docs are somewhat unclear, but win2k relies on this
       behavior.  */
    td->ctrl &= ~(TD_CTRL_ACTIVE | TD_CTRL_NAK);
    if (td->ctrl & TD_CTRL_IOC)
        *int_mask |= 0x01;

    if (pid == USB_PID_IN) {
        dma_bm_write(td->buffer, buf, len, 1);
        if ((td->ctrl & TD_CTRL_SPD) && len < max_len) {
            *int_mask |= 0x02;
            /* short packet: do not update QH */
            return TD_RESULT_NEXT_QH;
        }
    }

    /* success */
    return TD_RESULT_COMPLETE;
}


static int uhci_handle_td(usb_t *dev, uint32_t qh_addr,
                          UHCI_TD *td, uint32_t td_addr, uint32_t *int_mask)
{
    int ret;
    bool spd;
    uint8_t pid = td->token & 0xff;
    uint8_t addr, ep, i;
    usb_device_t* target = NULL;
    uint8_t buf[1024];
    uint32_t max_len, actual_length;

    /* Is active ? */
    if (!(td->ctrl & TD_CTRL_ACTIVE)) {
        /*
         * uhci11d spec page 22: "Even if the Active bit in the TD is already
         * cleared when the TD is fetched ... an IOC interrupt is generated"
         */
        if (td->ctrl & TD_CTRL_IOC) {
                *int_mask |= 0x01;
        }
        return TD_RESULT_NEXT_QH;
    }
    switch (pid) {
    case USB_PID_OUT:
    case USB_PID_SETUP:
    case USB_PID_IN:
        break;
    default:
        /* invalid pid : frame interrupted */
        dev->uhci_io_regs.status |= UHCI_STS_HCPERR;
        dev->uhci_io_regs.usbcmd &= ~UHCI_CMD_RS;
        uhci_update_irq(dev);
        return TD_RESULT_STOP_FRAME;
    }

    ep = (td->token >> 15) & 0xf;
    addr = (td->token >> 8) & 0x7f;

    for (i = 0; i < 2; i++) {
        if (!dev->uhci_devices[i])
            continue;

        if (dev->uhci_devices[i]->address != addr)
            continue;
        
        target = dev->uhci_devices[i];
        break;
    }
    if (!target) {
        return uhci_handle_td_error(dev, td, td_addr, USB_ERROR_NODEV,
                                        int_mask);
    }

    max_len = actual_length = ((td->token >> 21) + 1) & 0x7ff;
    spd = (pid == USB_PID_IN && (td->ctrl & TD_CTRL_SPD) != 0);

    switch(pid) {
        case USB_PID_OUT:
        case USB_PID_SETUP:
            dma_bm_read(td->buffer, buf, max_len, 1);
            ret = target->device_process(target->priv, buf, &max_len, pid, ep, spd);
            if (ret == USB_ERROR_NO_ERROR) {
                actual_length = max_len;
            }
            break;
        case USB_PID_IN:
            ret = target->device_process(target->priv, buf, &actual_length, pid, ep, spd);
            break;
    }
    ret = uhci_complete_td(dev, td, td_addr, ret, actual_length, buf, int_mask);
    return ret;
}

void
uhci_process_frame_schedule(void* p)
{
    usb_t *dev  = (usb_t *) p;
    uint32_t frame_list_base = (dev->uhci_io[UHCI_FLBASEADD + 1] << 8) | (dev->uhci_io[UHCI_FLBASEADD + 2] << 16) | (dev->uhci_io[UHCI_FLBASEADD + 3] << 24);
    uint32_t link = 1;
    uint16_t *regs = (uint16_t *) dev->uhci_io;
    UHCI_TD td;
    UHCI_QH qh;
    QhDb qhdb;
    uint32_t curr_qh = 0;
    uint32_t int_mask = 0;
    uint32_t td_count = 0;
    uint32_t cnt = 0;
    uint32_t old_td_ctrl = 0;
    uint32_t frame_bytes = 0;
    int ret;
    
    memset(&qhdb, 0, sizeof(QhDb));
    frame_list_base &= ~0xfff;
    frame_list_base |= (regs[UHCI_FRNUM >> 1] & 0x3ff) << 2;
    //pclog("UHCI: frame_list_base: 0x%08X\n", frame_list_base);
    //pclog("UHCI: frame number: %d\n", regs[UHCI_FRNUM >> 1] & 0x3ff);
    dma_bm_read(frame_list_base, (uint8_t*)&link, 4, 4);

    for (cnt = FRAME_MAX_LOOPS; is_valid(link) && cnt; cnt--) {
        if (frame_bytes >= 1280) {
            /* We've reached the usb 1.1 bandwidth, which is
               1280 bytes/frame, stop processing */
            break;
        }
        if (is_qh(link)) {
            /* QH */

            if (qhdb_insert(&qhdb, link)) {
                /*
                * We're going in circles. Which is not a bug because
                * HCD is allowed to do that as part of the BW management.
                *
                * Stop processing here if no transaction has been done
                * since we've been here last time.
                */
                if (td_count == 0) {
                    break;
                } else {
                    td_count = 0;
                    qhdb_reset(&qhdb);
                    qhdb_insert(&qhdb, link);
                }
            }

            dma_bm_read(link & ~0xf, (uint8_t*)&qh, sizeof(UHCI_QH), 4);
            if (!is_valid(qh.el_link)) {
                /* QH w/o elements */
                curr_qh = 0;
                link = qh.link;
            } else {
                /* QH with elements */
                curr_qh = link;
                link = qh.el_link;
            }
            continue;
        }

        old_td_ctrl = td.ctrl;
        ret = uhci_handle_td(dev, curr_qh, &td, link, &int_mask);

        if (old_td_ctrl != td.ctrl) {
            /* update the status bits of the TD */
            dma_bm_write((link & ~0xf) + 4, (uint8_t*)&td.ctrl, sizeof(td.ctrl), 4);
        }
        switch (ret) {
        case TD_RESULT_STOP_FRAME: /* interrupted frame */
            goto out;

        case TD_RESULT_NEXT_QH:
            link = curr_qh ? qh.link : td.link;
            continue;

        case TD_RESULT_COMPLETE:
            link = td.link;
            td_count++;
            frame_bytes += (td.ctrl & 0x7ff) + 1;

            if (curr_qh) {
                /* update QH element link */
                qh.el_link = link;
                dma_bm_write((curr_qh & ~0xf) + 4, (uint8_t*)&qh.el_link, sizeof(qh.el_link), 4);

                if (!depth_first(link)) {
                    /* done with this QH */
                    curr_qh = 0;
                    link    = qh.link;
                }
            }
            break;

        default:
            fatal("unknown return code");
        }

    }
out:
    if (int_mask) {
        dev->uhci_status_shadow |= int_mask;
        dev->uhci_io_regs.status |= UHCI_STS_USBINT;
        uhci_update_irq(dev);
    }
    if (dev->uhci_io_regs.usbcmd & UHCI_CMD_RS)
        timer_on_auto(&dev->uhci_frame_timer, 1000.);

    regs[UHCI_FRNUM >> 1]++;
    return;
}
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
#include <86box/fifo8.h>
#include <86box/timer.h>
#include <86box/usb.h>
#include "cpu.h"
#include <86box/plat_unused.h>

#include "usb_desc.h"

/* generic USB device helpers (you are not forced to use them when
   writing your USB device driver, but they help handling the
   protocol)
*/

#define SETUP_STATE_IDLE  0
#define SETUP_STATE_SETUP 1
#define SETUP_STATE_DATA  2
#define SETUP_STATE_ACK   3
#define SETUP_STATE_PARAM 4

static inline bool usb_packet_is_inflight(USBPacket *p)
{
    return (p->state == USB_PACKET_QUEUED ||
            p->state == USB_PACKET_ASYNC);
}

static void do_token_setup(USBDevice *s, USBPacket *p)
{
    int request, value, index;
    unsigned int setup_len;

    if (p->iov.size != 8) {
        p->status = USB_RET_STALL;
        return;
    }

    usb_packet_copy(p, s->setup_buf, p->iov.size);
    s->setup_index = 0;
    p->actual_length = 0;
    setup_len = (s->setup_buf[7] << 8) | s->setup_buf[6];
    if (setup_len > sizeof(s->data_buf)) {
        fprintf(stderr,
                "usb_generic_handle_packet: ctrl buffer too small (%u > %zu)\n",
                setup_len, sizeof(s->data_buf));
        p->status = USB_RET_STALL;
        return;
    }
    s->setup_len = setup_len;

    request = (s->setup_buf[0] << 8) | s->setup_buf[1];
    value   = (s->setup_buf[3] << 8) | s->setup_buf[2];
    index   = (s->setup_buf[5] << 8) | s->setup_buf[4];

    if (s->setup_buf[0] & USB_DIR_IN) {
        //usb_pcap_ctrl(p, true);
        usb_device_handle_control(s, p, request, value, index,
                                  s->setup_len, s->data_buf);
        if (p->status == USB_RET_ASYNC) {
            s->setup_state = SETUP_STATE_SETUP;
        }
        if (p->status != USB_RET_SUCCESS) {
            return;
        }

        if (p->actual_length < s->setup_len) {
            s->setup_len = p->actual_length;
        }
        s->setup_state = SETUP_STATE_DATA;
    } else {
        if (s->setup_len == 0)
            s->setup_state = SETUP_STATE_ACK;
        else
            s->setup_state = SETUP_STATE_DATA;
    }

    p->actual_length = 8;
}

static void do_token_in(USBDevice *s, USBPacket *p)
{
    int request, value, index;

    assert(p->ep->nr == 0);

    request = (s->setup_buf[0] << 8) | s->setup_buf[1];
    value   = (s->setup_buf[3] << 8) | s->setup_buf[2];
    index   = (s->setup_buf[5] << 8) | s->setup_buf[4];

    switch(s->setup_state) {
    case SETUP_STATE_ACK:
        if (!(s->setup_buf[0] & USB_DIR_IN)) {
            //usb_pcap_ctrl(p, true);
            usb_device_handle_control(s, p, request, value, index,
                                      s->setup_len, s->data_buf);
            if (p->status == USB_RET_ASYNC) {
                return;
            }
            s->setup_state = SETUP_STATE_IDLE;
            p->actual_length = 0;
            //usb_pcap_ctrl(p, false);
        }
        break;

    case SETUP_STATE_DATA:
        if (s->setup_buf[0] & USB_DIR_IN) {
            int len = s->setup_len - s->setup_index;
            if (len > p->iov.size) {
                len = p->iov.size;
            }
            usb_packet_copy(p, s->data_buf + s->setup_index, len);
            s->setup_index += len;
            if (s->setup_index >= s->setup_len) {
                s->setup_state = SETUP_STATE_ACK;
            }
            return;
        }
        s->setup_state = SETUP_STATE_IDLE;
        p->status = USB_RET_STALL;
        //usb_pcap_ctrl(p, false);
        break;

    default:
        p->status = USB_RET_STALL;
    }
}

static void do_token_out(USBDevice *s, USBPacket *p)
{
    assert(p->ep->nr == 0);

    switch(s->setup_state) {
    case SETUP_STATE_ACK:
        if (s->setup_buf[0] & USB_DIR_IN) {
            s->setup_state = SETUP_STATE_IDLE;
            //usb_pcap_ctrl(p, false);
            /* transfer OK */
        } else {
            /* ignore additional output */
        }
        break;

    case SETUP_STATE_DATA:
        if (!(s->setup_buf[0] & USB_DIR_IN)) {
            int len = s->setup_len - s->setup_index;
            if (len > p->iov.size) {
                len = p->iov.size;
            }
            usb_packet_copy(p, s->data_buf + s->setup_index, len);
            s->setup_index += len;
            if (s->setup_index >= s->setup_len) {
                s->setup_state = SETUP_STATE_ACK;
            }
            return;
        }
        s->setup_state = SETUP_STATE_IDLE;
        p->status = USB_RET_STALL;
        //usb_pcap_ctrl(p, false);
        break;

    default:
        p->status = USB_RET_STALL;
    }
}

static void do_parameter(USBDevice *s, USBPacket *p)
{
    int i, request, value, index;
    unsigned int setup_len;

    for (i = 0; i < 8; i++) {
        s->setup_buf[i] = p->parameter >> (i*8);
    }

    s->setup_state = SETUP_STATE_PARAM;
    s->setup_index = 0;

    request = (s->setup_buf[0] << 8) | s->setup_buf[1];
    value   = (s->setup_buf[3] << 8) | s->setup_buf[2];
    index   = (s->setup_buf[5] << 8) | s->setup_buf[4];

    setup_len = (s->setup_buf[7] << 8) | s->setup_buf[6];
    if (setup_len > sizeof(s->data_buf)) {
        fprintf(stderr,
                "usb_generic_handle_packet: ctrl buffer too small (%u > %zu)\n",
                setup_len, sizeof(s->data_buf));
        p->status = USB_RET_STALL;
        return;
    }
    s->setup_len = setup_len;

    if (p->pid == USB_TOKEN_OUT) {
        usb_packet_copy(p, s->data_buf, s->setup_len);
    }

    //usb_pcap_ctrl(p, true);
    usb_device_handle_control(s, p, request, value, index,
                              s->setup_len, s->data_buf);
    if (p->status == USB_RET_ASYNC) {
        return;
    }

    if (p->actual_length < s->setup_len) {
        s->setup_len = p->actual_length;
    }
    if (p->pid == USB_TOKEN_IN) {
        p->actual_length = 0;
        usb_packet_copy(p, s->data_buf, s->setup_len);
    }
    //usb_pcap_ctrl(p, false);
}

static void usb_process_one(USBPacket *p)
{
    USBDevice *dev = p->ep->dev;
    bool nak;

    /*
     * Handlers expect status to be initialized to USB_RET_SUCCESS, but it
     * can be USB_RET_NAK here from a previous usb_process_one() call,
     * or USB_RET_ASYNC from going through usb_queue_one().
     */
    nak = (p->status == USB_RET_NAK);
    p->status = USB_RET_SUCCESS;

    if (p->ep->nr == 0) {
        /* control pipe */
        if (p->parameter) {
            do_parameter(dev, p);
            return;
        }
        switch (p->pid) {
        case USB_TOKEN_SETUP:
            do_token_setup(dev, p);
            break;
        case USB_TOKEN_IN:
            do_token_in(dev, p);
            break;
        case USB_TOKEN_OUT:
            do_token_out(dev, p);
            break;
        default:
            p->status = USB_RET_STALL;
        }
    } else {
        /* data pipe */
        usb_device_handle_data(dev, p);
    }
}

static void usb_queue_one(USBPacket *p)
{
    /* Asynchronous completion isn't implemented yet... */
    fatal("usb_queue_one\n");
    usb_packet_set_state(p, USB_PACKET_QUEUED);
    QTAILQ_INSERT_TAIL(&p->ep->queue, p, queue);
    p->status = USB_RET_ASYNC;
}

/* Hand over a packet to a device for processing.  p->status ==
   USB_RET_ASYNC indicates the processing isn't finished yet, the
   driver will call usb_packet_complete() when done processing it. */
void usb_handle_packet(USBDevice *dev, USBPacket *p)
{
    if (dev == NULL) {
        p->status = USB_RET_NODEV;
        return;
    }
    assert(dev == p->ep->dev);
    assert(dev->state == USB_STATE_DEFAULT);
    usb_packet_check_state(p, USB_PACKET_SETUP);
    assert(p->ep != NULL);

    /* Submitting a new packet clears halt */
    if (p->ep->halted) {
        assert(QTAILQ_EMPTY(&p->ep->queue));
        p->ep->halted = false;
    }

    if (QTAILQ_EMPTY(&p->ep->queue) || p->ep->pipeline || p->stream) {
        usb_process_one(p);
        if (p->status == USB_RET_ASYNC) {
            /* hcd drivers cannot handle async for isoc */
            assert(p->ep->type != USB_ENDPOINT_XFER_ISOC);
            /* using async for interrupt packets breaks migration */
            assert(p->ep->type != USB_ENDPOINT_XFER_INT ||
                   (dev->flags & (1 << USB_DEV_FLAG_IS_HOST)));
            usb_packet_set_state(p, USB_PACKET_ASYNC);
            QTAILQ_INSERT_TAIL(&p->ep->queue, p, queue);
        } else if (p->status == USB_RET_ADD_TO_QUEUE) {
            usb_queue_one(p);
        } else {
            /*
             * When pipelining is enabled usb-devices must always return async,
             * otherwise packets can complete out of order!
             */
            assert(p->stream || !p->ep->pipeline ||
                   QTAILQ_EMPTY(&p->ep->queue));
            if (p->status != USB_RET_NAK) {
                //usb_pcap_data(p, false);
                usb_packet_set_state(p, USB_PACKET_COMPLETE);
            }
        }
    } else {
        usb_queue_one(p);
    }
}

/* Cancel an active packet.  The packed must have been deferred by
   returning USB_RET_ASYNC from handle_packet, and not yet
   completed.  */
void usb_cancel_packet(USBPacket * p)
{
    bool callback = (p->state == USB_PACKET_ASYNC);
    assert(usb_packet_is_inflight(p));
    usb_packet_set_state(p, USB_PACKET_CANCELED);
    QTAILQ_REMOVE(&p->ep->queue, p, queue);
    if (callback) {
        //usb_device_cancel_packet(p->ep->dev, p);
    }
}

void usb_packet_check_state(USBPacket *p, USBPacketState expected)
{
    if (p->state == expected) {
        return;
    }

    fatal("usb packet state check failed (expected 0x%02X, got 0x%02X)\n", expected, p->state);
}

void usb_packet_set_state(USBPacket *p, USBPacketState state)
{
    p->state = state;
}

void usb_packet_setup(USBPacket *p, int pid,
                      USBEndpoint *ep, unsigned int stream,
                      uint64_t id, bool short_not_ok, bool int_req)
{
    assert(!usb_packet_is_inflight(p));
    p->id = id;
    p->pid = pid;
    p->ep = ep;
    p->stream = stream;
    p->status = USB_RET_SUCCESS;
    p->actual_length = 0;
    p->parameter = 0;
    p->short_not_ok = short_not_ok;
    p->int_req = int_req;
    p->iov.ptr = NULL;
    p->iov.size = 0;
    usb_packet_set_state(p, USB_PACKET_SETUP);
}

void usb_packet_addbuf(USBPacket *p, void *ptr, size_t len)
{
    if (!len)
        return; /* Nothing to add. */
    if (p->iov.size == 0)
    {
        p->iov.ptr = calloc(1, len);
        p->iov.size += len;
        memcpy(p->iov.ptr, ptr, len);
    } else{
        p->iov.ptr = realloc(p->iov.ptr, p->iov.size + len);
        memcpy(p->iov.ptr + p->iov.size, ptr, len);
        p->iov.size += len;
    }
}

void usb_packet_copy(USBPacket *p, void *ptr, size_t bytes)
{
    switch (p->pid) {
        case USB_TOKEN_SETUP:
        case USB_TOKEN_OUT:
            memcpy(ptr, p->iov.ptr + p->actual_length, bytes);
            break;
        case USB_TOKEN_IN:
            memcpy(p->iov.ptr + p->actual_length, ptr, bytes);
            break;
    }
    p->actual_length += bytes;
}

void usb_packet_skip(USBPacket *p, size_t bytes)
{
    if (p->pid == USB_TOKEN_IN) {
        memset(p->iov.ptr + p->actual_length, 0, bytes);
    }
    p->actual_length += bytes;
}

size_t usb_packet_size(USBPacket *p)
{
    return p->iov.size;
}

void usb_packet_cleanup(USBPacket *p)
{
    //assert(!usb_packet_is_inflight(p));
    free(p->iov.ptr);
    p->iov.size = 0;
}

/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Definitions for the Distributed DMA emulation.
 *
 *
 *
 * Authors:	Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2020 Miran Grca.
 */

#ifndef USB_H
#define USB_H

#ifdef __cplusplus
extern "C" {
#endif

#include <86box/queue.h>
#include <stdbool.h>

#if defined(_WIN32) && (defined(__x86_64__) || defined(__i386__))
# define STRUCT_PACKED __attribute__((packed))
#else
# define STRUCT_PACKED __attribute__((packed))
#endif

/* Constants related to the USB / PCI interaction */
#define USB_SBRN    0x60 /* Serial Bus Release Number Register */
#define USB_RELEASE_1  0x10 /* USB 1.0 */
#define USB_RELEASE_2  0x20 /* USB 2.0 */
#define USB_RELEASE_3  0x30 /* USB 3.0 */

#define USB_TOKEN_SETUP 0x2d
#define USB_TOKEN_IN    0x69 /* device -> host */
#define USB_TOKEN_OUT   0xe1 /* host -> device */

#define USB_RET_SUCCESS           (0)
#define USB_RET_NODEV             (-1)
#define USB_RET_NAK               (-2)
#define USB_RET_STALL             (-3)
#define USB_RET_BABBLE            (-4)
#define USB_RET_IOERROR           (-5)
#define USB_RET_ASYNC             (-6)
#define USB_RET_ADD_TO_QUEUE      (-7)
#define USB_RET_REMOVE_FROM_QUEUE (-8)

#define USB_SPEED_LOW   0
#define USB_SPEED_FULL  1
#define USB_SPEED_HIGH  2
#define USB_SPEED_SUPER 3

#define USB_SPEED_MASK_LOW   (1 << USB_SPEED_LOW)
#define USB_SPEED_MASK_FULL  (1 << USB_SPEED_FULL)
#define USB_SPEED_MASK_HIGH  (1 << USB_SPEED_HIGH)
#define USB_SPEED_MASK_SUPER (1 << USB_SPEED_SUPER)

#define USB_STATE_NOTATTACHED 0
#define USB_STATE_ATTACHED    1
//#define USB_STATE_POWERED     2
#define USB_STATE_DEFAULT     3
//#define USB_STATE_ADDRESS     4
//#define       USB_STATE_CONFIGURED  5
#define USB_STATE_SUSPENDED   6

#define USB_CLASS_AUDIO                 1
#define USB_CLASS_COMM                  2
#define USB_CLASS_HID                   3
#define USB_CLASS_PHYSICAL              5
#define USB_CLASS_STILL_IMAGE           6
#define USB_CLASS_PRINTER               7
#define USB_CLASS_MASS_STORAGE          8
#define USB_CLASS_HUB                   9
#define USB_CLASS_CDC_DATA              0x0a
#define USB_CLASS_CSCID                 0x0b
#define USB_CLASS_CONTENT_SEC           0x0d
#define USB_CLASS_APP_SPEC              0xfe
#define USB_CLASS_VENDOR_SPEC           0xff

#define USB_SUBCLASS_UNDEFINED          0
#define USB_SUBCLASS_AUDIO_CONTROL      1
#define USB_SUBCLASS_AUDIO_STREAMING    2
#define USB_SUBCLASS_AUDIO_MIDISTREAMING 3

#define USB_DIR_OUT                     0
#define USB_DIR_IN                      0x80

#define USB_TYPE_MASK                   (0x03 << 5)
#define USB_TYPE_STANDARD               (0x00 << 5)
#define USB_TYPE_CLASS                  (0x01 << 5)
#define USB_TYPE_VENDOR                 (0x02 << 5)
#define USB_TYPE_RESERVED               (0x03 << 5)

#define USB_RECIP_MASK                  0x1f
#define USB_RECIP_DEVICE                0x00
#define USB_RECIP_INTERFACE             0x01
#define USB_RECIP_ENDPOINT              0x02
#define USB_RECIP_OTHER                 0x03

#define DeviceRequest ((USB_DIR_IN|USB_TYPE_STANDARD|USB_RECIP_DEVICE)<<8)
#define DeviceOutRequest ((USB_DIR_OUT|USB_TYPE_STANDARD|USB_RECIP_DEVICE)<<8)
#define VendorDeviceRequest ((USB_DIR_IN|USB_TYPE_VENDOR|USB_RECIP_DEVICE)<<8)
#define VendorDeviceOutRequest \
        ((USB_DIR_OUT|USB_TYPE_VENDOR|USB_RECIP_DEVICE)<<8)

#define InterfaceRequest                                        \
        ((USB_DIR_IN|USB_TYPE_STANDARD|USB_RECIP_INTERFACE)<<8)
#define InterfaceOutRequest \
        ((USB_DIR_OUT|USB_TYPE_STANDARD|USB_RECIP_INTERFACE)<<8)
#define ClassInterfaceRequest \
        ((USB_DIR_IN|USB_TYPE_CLASS|USB_RECIP_INTERFACE)<<8)
#define ClassInterfaceOutRequest \
        ((USB_DIR_OUT|USB_TYPE_CLASS|USB_RECIP_INTERFACE)<<8)
#define VendorInterfaceRequest \
        ((USB_DIR_IN|USB_TYPE_VENDOR|USB_RECIP_INTERFACE)<<8)
#define VendorInterfaceOutRequest \
        ((USB_DIR_OUT|USB_TYPE_VENDOR|USB_RECIP_INTERFACE)<<8)

#define EndpointRequest ((USB_DIR_IN|USB_TYPE_STANDARD|USB_RECIP_ENDPOINT)<<8)
#define EndpointOutRequest \
        ((USB_DIR_OUT|USB_TYPE_STANDARD|USB_RECIP_ENDPOINT)<<8)

#define USB_REQ_GET_STATUS              0x00
#define USB_REQ_CLEAR_FEATURE           0x01
#define USB_REQ_SET_FEATURE             0x03
#define USB_REQ_SET_ADDRESS             0x05
#define USB_REQ_GET_DESCRIPTOR          0x06
#define USB_REQ_SET_DESCRIPTOR          0x07
#define USB_REQ_GET_CONFIGURATION       0x08
#define USB_REQ_SET_CONFIGURATION       0x09
#define USB_REQ_GET_INTERFACE           0x0A
#define USB_REQ_SET_INTERFACE           0x0B
#define USB_REQ_SYNCH_FRAME             0x0C
#define USB_REQ_SET_SEL                 0x30
#define USB_REQ_SET_ISOCH_DELAY         0x31

#define USB_DEVICE_SELF_POWERED         0
#define USB_DEVICE_REMOTE_WAKEUP        1

#define USB_DT_DEVICE                   0x01
#define USB_DT_CONFIG                   0x02
#define USB_DT_STRING                   0x03
#define USB_DT_INTERFACE                0x04
#define USB_DT_ENDPOINT                 0x05
#define USB_DT_DEVICE_QUALIFIER         0x06
#define USB_DT_OTHER_SPEED_CONFIG       0x07
#define USB_DT_DEBUG                    0x0A
#define USB_DT_INTERFACE_ASSOC          0x0B
#define USB_DT_BOS                      0x0F
#define USB_DT_DEVICE_CAPABILITY        0x10
#define USB_DT_CS_INTERFACE             0x24
#define USB_DT_CS_ENDPOINT              0x25
#define USB_DT_ENDPOINT_COMPANION       0x30

#define USB_DEV_CAP_WIRELESS            0x01
#define USB_DEV_CAP_USB2_EXT            0x02
#define USB_DEV_CAP_SUPERSPEED          0x03

#define USB_CFG_ATT_ONE              (1 << 7) /* should always be set */
#define USB_CFG_ATT_SELFPOWER        (1 << 6)
#define USB_CFG_ATT_WAKEUP           (1 << 5)
#define USB_CFG_ATT_BATTERY          (1 << 4)

#define USB_ENDPOINT_XFER_CONTROL       0
#define USB_ENDPOINT_XFER_ISOC          1
#define USB_ENDPOINT_XFER_BULK          2
#define USB_ENDPOINT_XFER_INT           3
#define USB_ENDPOINT_XFER_INVALID     255

#define USB_INTERFACE_INVALID         255

typedef struct USBBusOps USBBusOps;
typedef struct USBPort USBPort;
typedef struct USBDevice USBDevice;
typedef struct USBPacket USBPacket;
typedef struct USBCombinedPacket USBCombinedPacket;
typedef struct USBEndpoint USBEndpoint;

typedef struct USBDesc USBDesc;
typedef struct USBDescID USBDescID;
typedef struct USBDescDevice USBDescDevice;
typedef struct USBDescConfig USBDescConfig;
typedef struct USBDescIfaceAssoc USBDescIfaceAssoc;
typedef struct USBDescIface USBDescIface;
typedef struct USBDescEndpoint USBDescEndpoint;
typedef struct USBDescOther USBDescOther;
typedef struct USBDescString USBDescString;
typedef struct USBDescMSOS USBDescMSOS;

enum usb_pid
{
    USB_PID_OUT   = 0xE1,
    USB_PID_IN    = 0x69,
    USB_PID_SETUP = 0x2D
};

struct USBDescString {
    uint8_t index;
    char *str;
    QLIST_ENTRY(USBDescString) next;
};

#define USB_MAX_ENDPOINTS  15
#define USB_MAX_INTERFACES 16

struct USBEndpoint {
    uint8_t nr;
    uint8_t pid;
    uint8_t type;
    uint8_t ifnum;
    int max_packet_size;
    int max_streams;
    bool pipeline;
    bool halted;
    USBDevice *dev;
    QTAILQ_HEAD(, USBPacket) queue;
};

enum USBDeviceFlags {
    USB_DEV_FLAG_IS_HOST,
    USB_DEV_FLAG_MSOS_DESC_ENABLE,
    USB_DEV_FLAG_MSOS_DESC_IN_USE,
    USB_DEV_FLAG_IS_SCSI_STORAGE,
};

/* Base USB descriptor struct. */
typedef struct usb_desc_base_t {
    uint8_t bLength;
    uint8_t bDescriptorType;
} usb_desc_base_t;

enum usb_desc_setup_req_types {
    USB_SETUP_TYPE_DEVICE    = 0x0,
    USB_SETUP_TYPE_INTERFACE = 0x1,
    USB_SETUP_TYPE_ENDPOINT  = 0x2,
    USB_SETUP_TYPE_OTHER     = 0x3,
};

/* definition of a USB device */
typedef struct USBDevice {
    char *serial;
    union {
        void *opaque;
        void *priv;
    };
    uint32_t flags;

    char *pcap_filename;
    FILE *pcap;

    /* Actual connected speed */
    int speed;
    /* Supported speeds, not in info because it may be variable (hostdevs) */
    int speedmask;
    uint8_t addr;
    char product_desc[32];
    int auto_attach;
    uint16_t port;
    bool attached;

    int32_t state;
    uint8_t setup_buf[8];
    uint8_t data_buf[4096];
    int32_t remote_wakeup;
    int32_t setup_state;
    int32_t setup_len;
    int32_t setup_index;

    USBEndpoint ep_ctl;
    USBEndpoint ep_in[USB_MAX_ENDPOINTS];
    USBEndpoint ep_out[USB_MAX_ENDPOINTS];

    QLIST_HEAD(, USBDescString) strings;
    const USBDesc *usb_desc; /* Overrides class usb_desc if not NULL */
    const USBDescDevice *device;

    int configuration;
    int ninterfaces;
    int altsetting[USB_MAX_INTERFACES];
    const USBDescConfig *config;
    const USBDescIface  *ifaces[USB_MAX_INTERFACES];

    /*
     * Called when a packet is canceled.
     */
    void (*cancel_packet)(USBDevice *dev, USBPacket *p);

    /*
     * Attach the device
     */
    void (*handle_attach)(USBDevice *dev);

    /*
     * Reset the device
     */
    void (*handle_reset)(USBDevice *dev);

    /*
     * Process control request.
     * Called from handle_packet().
     *
     * Status gets stored in p->status, and if p->status == USB_RET_SUCCESS
     * then the number of bytes transferred is stored in p->actual_length
     */
    void (*handle_control)(USBDevice *dev, USBPacket *p, int request, int value,
                           int index, int length, uint8_t *data);

    /*
     * Process data transfers (both BULK and ISOC).
     * Called from handle_packet().
     *
     * Status gets stored in p->status, and if p->status == USB_RET_SUCCESS
     * then the number of bytes transferred is stored in p->actual_length
     */
    void (*handle_data)(USBDevice *dev, USBPacket *p);

    void (*set_interface)(USBDevice *dev, int interface,
                          int alt_old, int alt_new);

    /*
     * Called when the hcd is done queuing packets for an endpoint, only
     * necessary for devices which can return USB_RET_ADD_TO_QUEUE.
     */
    void (*flush_ep_queue)(USBDevice *dev, USBEndpoint *ep);

    /*
     * Called by the hcd to let the device know the queue for an endpoint
     * has been unlinked / stopped. Optional may be NULL.
     */
    void (*ep_stopped)(USBDevice *dev, USBEndpoint *ep);

    bool attached_settable;
} USBDevice;

typedef enum USBPacketState {
    USB_PACKET_UNDEFINED = 0,
    USB_PACKET_SETUP,
    USB_PACKET_QUEUED,
    USB_PACKET_ASYNC,
    USB_PACKET_COMPLETE,
    USB_PACKET_CANCELED,
} USBPacketState;

/* Structure used to hold information about an active USB packet.  */
struct USBPacket {
    /* Data fields for use by the driver.  */
    int pid;
    uint64_t id;
    USBEndpoint *ep;
    unsigned int stream;
    struct 
    {
        void *ptr;
        size_t size;
    } iov;
    uint64_t parameter; /* control transfers */
    bool short_not_ok;
    bool int_req;
    int status; /* USB_RET_* status code */
    int actual_length; /* Number of bytes actually transferred */
    /* Internal use by the USB layer.  */
    USBPacketState state;
    //USBCombinedPacket *combined;
    QTAILQ_ENTRY(USBPacket) queue;
    QTAILQ_ENTRY(USBPacket) combined_entry;
};

typedef USBDevice usb_device_t;

typedef struct UHCIPort
{
    uint16_t ctrl;
    usb_device_t* dev;
} UHCIPort;

struct usb_t;
typedef struct usb_t usb_t;

typedef struct UHCIState {
    pc_timer_t frame_timer;
    uint32_t pending_int_mask;
    uint16_t cmd; /* cmd register */
    uint16_t status;
    uint16_t intr; /* interrupt enable register */
    uint16_t frnum; /* frame number */
    uint32_t fl_base_addr; /* frame list base address */
    uint32_t frame_bytes;
    uint8_t sof_timing;
    uint8_t status2; /* bit 0 and 1 are used to generate UHCI_STS_USBINT */
    uint8_t irq_state;

    UHCIPort ports[2];
    usb_t* dev;
} UHCIState;

typedef struct usb_params_t
{
    int pci_slot;
    uint8_t* pci_regs;
} usb_params_t;

typedef struct usb_t {
    UHCIState     uhci_state;
    uint8_t       ohci_mmio[4096];
    uint16_t      uhci_io_base;
    int           uhci_enable;
    int           ohci_enable;
    uint32_t      ohci_mem_base;
    mem_mapping_t ohci_mmio_mapping;
    int           inst_cnt;
    usb_params_t params;
} usb_t;

enum usb_bus_types
{
    USB_BUS_OHCI = 0,
    USB_BUS_UHCI = 1,
    USB_BUS_MAX  = 2
};

/* Global variables. */
extern const device_t usb_device;

/* Functions. */
extern void uhci_update_io_mapping(usb_t *dev, uint8_t base_l, uint8_t base_h, int enable);
extern void ohci_update_mem_mapping(usb_t *dev, uint8_t base1, uint8_t base2, uint8_t base3, int enable);
extern uint16_t usb_attach_device(usb_device_t* device, uint8_t bus_type);
void usb_detach_device(uint16_t port);
extern uint8_t usb_parse_control_endpoint(usb_device_t* usb_device, uint8_t* data, uint32_t *len, uint8_t pid_token, uint8_t endpoint, uint8_t underrun_not_allowed);

void usb_device_handle_control(USBDevice *dev, USBPacket *p, int request,
                               int val, int index, int length, uint8_t *data);

void usb_device_handle_data(USBDevice *dev, USBPacket *p);

void usb_device_set_interface(USBDevice *dev, int interface,
                              int alt_old, int alt_new);

void usb_device_init(USBDevice *dev, const char* product_desc);

void usb_device_reset(USBDevice *dev);

extern const USBDesc *usb_device_get_usb_desc(USBDevice *dev);

void usb_packet_init(USBPacket *p);
void usb_packet_set_state(USBPacket *p, USBPacketState state);
void usb_packet_check_state(USBPacket *p, USBPacketState expected);
void usb_packet_setup(USBPacket *p, int pid,
                      USBEndpoint *ep, unsigned int stream,
                      uint64_t id, bool short_not_ok, bool int_req);
void usb_packet_addbuf(USBPacket *p, void *ptr, size_t len);
void usb_packet_copy(USBPacket *p, void *ptr, size_t bytes);
void usb_packet_skip(USBPacket *p, size_t bytes);
size_t usb_packet_size(USBPacket *p);
void usb_packet_cleanup(USBPacket *p);

void usb_handle_packet(USBDevice *dev, USBPacket *p);
void usb_packet_complete(USBDevice *dev, USBPacket *p);
void usb_packet_complete_one(USBDevice *dev, USBPacket *p);
void usb_cancel_packet(USBPacket * p);

void usb_ep_init(USBDevice *dev);
void usb_ep_reset(USBDevice *dev);
void usb_ep_dump(USBDevice *dev);
struct USBEndpoint *usb_ep_get(USBDevice *dev, int pid, int ep);
uint8_t usb_ep_get_type(USBDevice *dev, int pid, int ep);
void usb_ep_set_type(USBDevice *dev, int pid, int ep, uint8_t type);
void usb_ep_set_ifnum(USBDevice *dev, int pid, int ep, uint8_t ifnum);
void usb_ep_set_max_packet_size(USBDevice *dev, int pid, int ep,
                                uint16_t raw);
void usb_ep_set_max_streams(USBDevice *dev, int pid, int ep, uint8_t raw);
void usb_ep_set_halted(USBDevice *dev, int pid, int ep, bool halted);
USBPacket *usb_ep_find_packet_by_id(USBDevice *dev, int pid, int ep,
                                    uint64_t id);

#ifdef __cplusplus
}
#endif

#endif /*USB_H*/

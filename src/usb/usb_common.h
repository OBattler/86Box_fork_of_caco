#ifndef USB_COMMON_H
#define USB_COMMON_H

#include <stdbool.h>

typedef unsigned long long Bit64u;
typedef long long          Bit64s;
typedef unsigned int       Bit32u;
typedef   signed int       Bit32s;
typedef unsigned short int Bit16u;
typedef   signed short int Bit16s;
typedef unsigned char      Bit8u;
typedef   signed char      Bit8s;

// for the Packet Capture code to work, these four must remain as is
enum {
  USB_TRANS_TYPE_ISO     = 0,
  USB_TRANS_TYPE_INT     = 1,
  USB_TRANS_TYPE_CONTROL = 2,
  USB_TRANS_TYPE_BULK    = 3
};

#define USB_CONTROL_EP     0

#define USB_TOKEN_IN    0x69
#define USB_TOKEN_OUT   0xE1
#define USB_TOKEN_SETUP 0x2D

#define USB_MSG_ATTACH   0x100
#define USB_MSG_DETACH   0x101
#define USB_MSG_RESET    0x102

#define USB_RET_NODEV   (-1)
#define USB_RET_NAK     (-2)
#define USB_RET_STALL   (-3)
#define USB_RET_BABBLE  (-4)
#define USB_RET_IOERROR (-5)
#define USB_RET_ASYNC   (-6)

// these must remain in this order, 0 -> 3
enum {
  USB_SPEED_LOW   = 0,
  USB_SPEED_FULL  = 1,
  USB_SPEED_HIGH  = 2,
  USB_SPEED_SUPER = 3
};

enum {
  USB_STATE_NOTATTACHED = 0,
  USB_STATE_ATTACHED    = 1,
//USB_STATE_POWERED     = 2,
  USB_STATE_DEFAULT     = 3,
  USB_STATE_ADDRESS     = 4,
  USB_STATE_CONFIGURED  = 5,
  USB_STATE_SUSPENDED   = 6
};

#define USB_DIR_OUT  0
#define USB_DIR_IN   0x80

#define USB_TYPE_MASK            (0x03 << 5)
#define USB_TYPE_STANDARD        (0x00 << 5)
#define USB_TYPE_CLASS           (0x01 << 5)
#define USB_TYPE_VENDOR          (0x02 << 5)
#define USB_TYPE_RESERVED        (0x03 << 5)

#define USB_RECIP_MASK            0x1f
#define USB_RECIP_DEVICE          0x00
#define USB_RECIP_INTERFACE       0x01
#define USB_RECIP_ENDPOINT        0x02
#define USB_RECIP_OTHER           0x03

#define DeviceRequest ((USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_DEVICE) << 8)     // Host to device / Standard Type / Recipient:Device
#define DeviceOutRequest ((USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_DEVICE) << 8) // Device to host / Standard Type / Recipient:Device
#define DeviceClassInRequest \
   ((USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_DEVICE) << 8)
#define InterfaceRequest \
   ((USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_INTERFACE) << 8)
#define InterfaceInClassRequest \
   ((USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE) << 8)
#define OtherInClassRequest \
   ((USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_OTHER) << 8)
#define InterfaceOutRequest \
   ((USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_INTERFACE) << 8)
#define DeviceOutClassRequest \
   ((USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_DEVICE) << 8)
#define InterfaceOutClassRequest \
   ((USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE) << 8)
#define OtherOutClassRequest \
   ((USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_OTHER) << 8)
#define EndpointRequest ((USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_ENDPOINT) << 8)
#define EndpointOutRequest \
   ((USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_ENDPOINT) << 8)

#define USB_REQ_GET_STATUS        0x00
#define USB_REQ_CLEAR_FEATURE     0x01
#define USB_REQ_SET_FEATURE       0x03
#define USB_REQ_SET_ADDRESS       0x05
#define USB_REQ_GET_DESCRIPTOR    0x06
#define USB_REQ_SET_DESCRIPTOR    0x07
#define USB_REQ_GET_CONFIGURATION 0x08
#define USB_REQ_SET_CONFIGURATION 0x09
#define USB_REQ_GET_INTERFACE     0x0A
#define USB_REQ_SET_INTERFACE     0x0B
#define USB_REQ_SYNCH_FRAME       0x0C
#define USB_REQ_SET_SEL           0x30
#define USB_REQ_SET_ISO_DELAY     0x31

#define USB_ENDPOINT_HALT          0
#define USB_DEVICE_SELF_POWERED    0
#define USB_DEVICE_REMOTE_WAKEUP   1
#define USB_DEVICE_U1_ENABLE      48
#define USB_DEVICE_U2_ENABLE      49

// USB 1.1
#define USB_DT_DEVICE              0x01
#define USB_DT_CONFIG              0x02
#define USB_DT_STRING              0x03
#define USB_DT_INTERFACE           0x04
#define USB_DT_ENDPOINT            0x05
// USB 2.0
#define USB_DT_DEVICE_QUALIFIER         0x06
#define USB_DT_OTHER_SPEED_CONFIG       0x07
#define USB_DT_INTERFACE_POWER          0x08
// USB 3.0
#define USB_DT_BIN_DEV_OBJ_STORE        0x0F

typedef struct USBPacket USBPacket;

// packet events
#define USB_EVENT_WAKEUP        0
#define USB_EVENT_ASYNC         1
// controller events
#define USB_EVENT_DEFAULT_SPEED  10
#define USB_EVENT_CHECK_SPEED    11

// set this to 1 to monitor the TD's toggle bit
// setting to 0 will speed up the emualtion slightly
#define HANDLE_TOGGLE_CONTROL 1

#define USB_MAX_ENDPOINTS   5   // we currently don't use more than 5 endpoints (ep0, ep1, ep2, ep3, and ep4)

typedef int USBCallback(int event, void *ptr, void *dev, int port);

struct usb_device_c;
typedef struct usb_device_c usb_device_c;

struct USBPacket {
  int pid;
  Bit8u devaddr;
  Bit8u devep;
  Bit8u speed;           // packet's speed definition
#if HANDLE_TOGGLE_CONTROL
  int   toggle;          // packet's toggle bit (0, 1, or -1 for xHCI)
#endif
  Bit8u *data;
  int len;
  USBCallback *complete_cb;
  void *complete_dev;
  usb_device_c *dev;
  int strm_pid;         // stream primary id
};

typedef struct USBAsync {
  USBPacket packet;
  Bit64u    td_addr;
  bool done;
  Bit16u  slot_ep;
  struct USBAsync *next;
} USBAsync;

// Items about the endpoint gathered from various places
// These values are set at init() time, this is so we
//  don't have to parse the descriptors at runtime.
typedef struct USBEndPoint {
  int  max_packet_size;  // endpoint max packet size
  int  max_burst_size;   // endpoint max burst size (super-speed endpoint companion only)
#if HANDLE_TOGGLE_CONTROL
  int  toggle;           // the current toggle for the endpoint (0, 1, or -1 for xHCI)
#endif
  bool halted;           // is the current ep halted?
} USBEndPoint;

struct usb_device_c {
    Bit8u type;
    bool connected;
    int minspeed;  // must be no more than FULL speed for *any* device
    int maxspeed;
    int speed;
    Bit8u addr;
    Bit8u config;
    Bit8u alt_iface;
    Bit8u alt_iface_max;
    char devname[32];
    USBEndPoint endpoint_info[USB_MAX_ENDPOINTS];

    bool first8;
    const Bit8u *dev_descriptor;
    const Bit8u *config_descriptor;
    int device_desc_size;
    int config_desc_size;
    const char *vendor_desc;
    const char *product_desc;
    const char *serial_num;

    int state;
    Bit8u setup_buf[8];
    Bit8u data_buf[1024];
    int remote_wakeup;
    int setup_state;
    int setup_len;
    int setup_index;
    bool stall;
    bool async_mode;
    struct {
      USBCallback *cb;
      void *dev;
      int port;
    } event;

    void (*cancel_packet)(usb_device_c* device, USBPacket* p);
    int (*handle_packet)(usb_device_c* device, USBPacket* p);
    void (*handle_reset)(usb_device_c* device);
    int (*handle_control)(usb_device_c* device, int request, int value, int index, int length, Bit8u *data);
    int (*handle_data)(usb_device_c* device, USBPacket *p);
    void (*handle_iface_change)(usb_device_c* device, int iface);
    bool (*init)(usb_device_c *init);
};

void usb_device_create(usb_device_c* device);
void usb_packet_init(USBPacket *p, int size);
void usb_packet_cleanup(USBPacket *p);
void usb_defer_packet(USBPacket *p, usb_device_c *dev);
void usb_cancel_packet(USBPacket *p);
void usb_packet_complete(USBPacket *p);
USBAsync* create_async_packet(USBAsync **base, Bit64u addr, int maxlen);
void remove_async_packet(USBAsync **base, USBAsync *p);
USBAsync* find_async_packet(USBAsync **base, Bit64u addr);

void usb_device_set_event_handler(usb_device_c* device, void *dev, USBCallback *cb, int port);
void usb_device_send_msg(usb_device_c* device, int msg);
int usb_device_handle_control_common(usb_device_c* device, int request, int value, int index, int length, Bit8u *data);
bool usb_device_get_halted(usb_device_c* device, int ep);
int usb_set_usb_string(Bit8u *buf, const char *str);
int usb_device_get_mps(usb_device_c* device, const int ep);

#endif
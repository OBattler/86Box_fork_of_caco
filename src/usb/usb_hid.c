#include "86box/timer.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/usb.h>
#include <86box/dma.h>
#include "cpu.h"
#include <86box/pci.h>

#include "usb_common.h"

int
usb_device_hid_handle_data(usb_device_c *device, USBPacket *p);

// Physical Descriptor Items
enum {
    HID_PHYS_BIAS_NOT_APP     = 0,
    HID_PHYS_BIAS_RIGHT_HAND  = 1,
    HID_PHYS_BIAS_LEFT_HAND   = 2,
    HID_PHYS_BIAS_BOTH_HANDS  = 3,
    HID_PHYS_BIAS_EITHER_HAND = 4
};

typedef enum {
    None = 0,
    Hand,
    Eyeball,
    Eyebrow,
    Eyelid,
    Ear,
    Nose,
    Mouth,
    UpperLip,
    LowerLip,
    Jaw,
    Neck,
    UpperArm,
    Elbow,
    Forearm,
    Wrist,
    Palm,
    Thumb,
    IndexFinger,
    MiddleFinger,
    RingFinger,
    LittleFinger,
    Head,
    Shoulder,
    Hip,
    Waist,
    Thigh,
    Knee,
    Calf,
    Ankle,
    Foot,
    Heel,
    BallofFoot,
    BigToe,
    SecondToe,
    ThirdToe,
    FourthToe,
    LittleToe,
    Brow,
    Cheek,
} HID_PHYS_DESIGNATOR;

typedef enum {
    NotApp = 0,
    Right,
    Left,
    Both,
    Either,
    Center
} HID_PHYS_QUALIFIER;

// Device Model types
typedef enum {
    // mice (w/o physical descriptor)
    hid_mouse_2x2x8 = 0,     // 2-button, 2-coords: X and Y coords, 8-bit
    hid_mouse_3x3x8,         // 3-button, 3-coords: X, Y, and Wheel coords, 8-bit (default)
    hid_mouse_3x3x12,        // 3-button, 3-coords: X, Y, and Wheel coords, 12-bit
    hid_mouse_3x3xDebug,     // 3-button, 3-coords: X, Y, and Wheel coords (debug)
    hid_mouse_3x3x16,        // 3-button, 3-coords: X, Y, and Wheel coords, 16-bit
                             // mice (w/ physical descriptor)
    hid_mouse_3x3x8phy = 10, // 3-button, 3-coords: X, Y, and Wheel coords, 8-bit, Physical Descriptor included

    // keyboards

    // keypads

    // tablets

} HID_MODEL;

// one (or more) of our models uses the Report ID field. This is the ID value used.
#define HID_REPORT_ID 1

// our HID device(s) return two class specific strings, index 4 and index 5
#define HID_CLASS_STR4     4
#define HID_CLASS_STR5     5

#define BX_M_ELEMENTS_SIZE 8
#define BX_KBD_ELEMENTS    16

struct usb_device_hid {
    usb_device_c device;
    struct HID_STATE {
        bool   has_events;
        Bit8u  idle;
        int    mouse_delayed_dx;
        int    mouse_delayed_dy;
        Bit16s mouse_x;
        Bit16s mouse_y;
        Bit8s  mouse_z;
        Bit8u  b_state;
        Bit8u  mouse_event_count;
        Bit8u  mouse_event_buf[BX_KBD_ELEMENTS][BX_M_ELEMENTS_SIZE];
        int    mouse_event_buf_len[BX_KBD_ELEMENTS];
        Bit8u  kbd_packet[8];
        int    kbd_packet_indx;
        Bit8u  indicators;
        Bit8u  kbd_event_count;
        Bit32u kbd_event_buf[BX_KBD_ELEMENTS];
        // the remaining does not get cleared on a handle_reset()
        HID_MODEL    model;
        Bit8u        report_use_id; // id that we will use as soon as the HID report has been requested
        Bit8u        report_id;     // id that we will use after the HID report has been requested
        bool         boot_protocol; // 0 = boot protocol, 1 = report protocol
        int          bx_mouse_hid_report_descriptor_len;
        const Bit8u *bx_mouse_hid_report_descriptor;
    } s;
    pc_timer_t idle_timer;
};

typedef struct usb_device_hid usb_device_hid;

/* supported HID device types */
#define USB_HID_TYPE_MOUSE    0
#define USB_HID_TYPE_TABLET   1
#define USB_HID_TYPE_KEYPAD   2
#define USB_HID_TYPE_KEYBOARD 3

/* HID IDLE time constant */
#define HID_IDLE_TIME 4000

/* HID interface requests */
#define GET_REPORT   0x01
#define GET_IDLE     0x02
#define GET_PROTOCOL 0x03
#define SET_REPORT   0x09
#define SET_IDLE     0x0A
#define SET_PROTOCOL 0x0B

/* BOOT Protocol or Report Protocol */
#define PROTOCOL_BOOT   0
#define PROTOCOL_REPORT 1

// If you change any of the Max Packet Size, or other fields within these
//  descriptors, you must also change the d.endpoint_info[] array
//  to match your changes.

////////////////////////////////////////////////
// Mouse
static const Bit8u bx_mouse_dev_descriptor[] = {
    0x12,       /*  u8 bLength; */
    0x01,       /*  u8 bDescriptorType; Device */
    0x01, 0x01, /*  u16 bcdUSB; v1.1 */

    0x00, /*  u8  bDeviceClass; */
    0x00, /*  u8  bDeviceSubClass; */
    0x00, /*  u8  bDeviceProtocol; */
    0x08, /*  u8  bMaxPacketSize; 8 Bytes */

    0x27, 0x06, /*  u16 idVendor; */
    0x01, 0x00, /*  u16 idProduct; */
    0x00, 0x00, /*  u16 bcdDevice */

    0x01, /*  u8  iManufacturer; */
    0x02, /*  u8  iProduct; */
    0x03, /*  u8  iSerialNumber; */
    0x01  /*  u8  bNumConfigurations; */
};

static const Bit8u bx_mouse_dev_descriptor2[] = {
    0x12,       /*  u8 bLength; */
    0x01,       /*  u8 bDescriptorType; Device */
    0x00, 0x02, /*  u16 bcdUSB; v2.0 */

    0x00, /*  u8  bDeviceClass; */
    0x00, /*  u8  bDeviceSubClass; */
    0x00, /*  u8  bDeviceProtocol; */
    0x40, /*  u8  bMaxPacketSize; 64 Bytes */

    0x27, 0x06, /*  u16 idVendor; */
    0x01, 0x00, /*  u16 idProduct; */
    0x00, 0x00, /*  u16 bcdDevice */

    0x01, /*  u8  iManufacturer; */
    0x02, /*  u8  iProduct; */
    0x03, /*  u8  iSerialNumber; */
    0x01  /*  u8  bNumConfigurations; */
};

// The following hid report descriptors are for the mouse/tablet
//  depending on the model given.

// hid_mouse_2x2x8
// default 2-button, 3-byte, X and Y coords, 8-bit (+ report id)
// 00000001 (report id)
// 000000BB
// XXXXXXXX
// YYYYYYYY
static const Bit8u bx_mouse_hid_report_descriptor_228[] = {
    0x05, 0x01,           // Usage Page (Generic Desktop Ctrls)
    0x09, 0x02,           // Usage (Mouse)
    0xA1, 0x01,           // Collection (Application)
    0x09, 0x01,           //   Usage (Pointer)
    0x85, HID_REPORT_ID,  //   Report ID (HID_REPORT_ID)
    0x89, HID_CLASS_STR4, // Starting String Index (4)
    0x99, HID_CLASS_STR5, // Ending String Index (5)
    0xA1, 0x00,           //   Collection (Physical)
    0x05, 0x09,           //     Usage Page (Button)
    0x19, 0x01,           //     Usage Minimum (0x01)
    0x29, 0x02,           //     Usage Maximum (0x02)
    0x15, 0x00,           //     Logical Minimum (0)
    0x25, 0x01,           //     Logical Maximum (1)
    0x95, 0x02,           //     Report Count (2)
    0x75, 0x01,           //     Report Size (1)
    0x81, 0x02,           //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x95, 0x01,           //     Report Count (1)
    0x75, 0x06,           //     Report Size (6)
    0x81, 0x01,           //     Input (Const,Array,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x05, 0x01,           //     Usage Page (Generic Desktop Ctrls)
    0x09, 0x30,           //     Usage (X)
    0x09, 0x31,           //     Usage (Y)
    0x95, 0x02,           //     Report Count (2)
    0x15, 0x80,           //     Logical Minimum (-128)
    0x25, 0x7F,           //     Logical Maximum (127)
    0x75, 0x08,           //     Report Size (8)
    0x81, 0x06,           //     Input (Data,Var,Rel,No Wrap,Linear,Preferred State,No Null Position)
    0xC0,                 //   End Collection
    0xC0                  // End Collection
};

// hid_mouse_3x3x8
// 3-button, 4-byte X, Y, and Wheel coords, 8-bit
// 00000BBB
// XXXXXXXX
// YYYYYYYY
// WWWWWWWW
static const Bit8u bx_mouse_hid_report_descriptor_338[] = {
    0x05, 0x01,           // Usage Page (Generic Desktop Ctrls)
    0x09, 0x02,           // Usage (Mouse)
    0xA1, 0x01,           // Collection (Application)
    0x09, 0x01,           //   Usage (Pointer)
    0x89, HID_CLASS_STR4, // Starting String Index (4)
    0x99, HID_CLASS_STR5, // Ending String Index (5)
    0xA1, 0x00,           //   Collection (Physical)
    0x05, 0x09,           //     Usage Page (Button)
    0x15, 0x00,           //     Logical Minimum (0)
    0x25, 0x01,           //     Logical Maximum (1)
    0x19, 0x01,           //     Usage Minimum (0x01)
    0x29, 0x03,           //     Usage Maximum (0x03)
    0x95, 0x03,           //     Report Count (3)
    0x75, 0x01,           //     Report Size (1)
    0x81, 0x02,           //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x95, 0x01,           //     Report Count (1)
    0x75, 0x05,           //     Report Size (5)
    0x81, 0x01,           //     Input (Const,Array,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x05, 0x01,           //     Usage Page (Generic Desktop Ctrls)
    0x09, 0x30,           //     Usage (X)
    0x09, 0x31,           //     Usage (Y)
    0x09, 0x38,           //     Usage (Wheel)
    0x95, 0x03,           //     Report Count (3)
    0x15, 0x80,           //     Logical Minimum (-128)
    0x25, 0x7F,           //     Logical Maximum (127)
    0x75, 0x08,           //     Report Size (8)
    0x81, 0x06,           //     Input (Data,Var,Rel,No Wrap,Linear,Preferred State,No Null Position)
    0xC0,                 //   End Collection
    0xC0,                 // End Collection
};

// this model is deliberately irregular by design, so that a Guest can test its HID Report Descriptor Parser.
//  - the button fields are not consecutive and are at arbitrary positions in the report.
//  - the coords fields are not byte aligned or consecutively spaced.
//  - the coords fields are of an irregular size, each a different size.
//  - there are padding fields between entries that *do not* align the next field on a byte boundary.
//  - this also uses he push/pop mechanism to test the function of your parser.
// (Again, this is deliberate. A correctly written parser will extract the neccessary fields no matter the irregularity)
// hid_mouse_3x3xDebug
// 3-button, 5-byte X, Y, and Wheel coords (debug model)
// YYYYYYY0 - 10 bit Y displacement
// WWWW0YYY - 8 bit W displacement
// 0B00WWWW - bit 6 is Button #2 (right button)
// XXXXX0B0 - 9 bit X displacement, bit 1 is Button #1 (left button)
// 0B00XXXX - bit 6 is Button #3 (middle button)
static const Bit8u bx_mouse_hid_report_descriptor_33debug[] = {
    0x05, 0x01,       // Usage Page (Generic Desktop Ctrls)
    0x09, 0x02,       // Usage (Mouse)
    0xA1, 0x01,       // Collection (Application)
    0x09, 0x01,       //   Usage (Pointer)
    0xA1, 0x00,       //   Collection (Physical)
    0x95, 0x01,       //     Report Count (1)
    0x75, 0x01,       //     Report Size (1)
    0x15, 0x00,       //     Logical Minimum (0)
    0x25, 0x00,       //     Logical Maximum (0)
    0x81, 0x01,       //     Input (Const,Array,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x09, 0x31,       //     Usage (Y)
    0x75, 0x0A,       //     Report Size (10)
    0x16, 0x00, 0xFE, //     Logical Minimum (-512)
    0x26, 0xFF, 0x01, //     Logical Maximum (511)
    0x81, 0x06,       //     Input (Data,Var,Rel,No Wrap,Linear,Preferred State,No Null Position)
    0x75, 0x01,       //     Report Size (1)
    0x81, 0x01,       //     Input (Const,Array,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x09, 0x38,       //     Usage (Wheel)
    0x75, 0x08,       //     Report Size (8)
    0x15, 0x80,       //     Logical Minimum (-128)
    0x25, 0x7F,       //     Logical Maximum (127)
    0x81, 0x06,       //     Input (Data,Var,Rel,No Wrap,Linear,Preferred State,No Null Position)
    0xA4,             //     Push
    0x75, 0x01,       //       Report Size (1)
    0x95, 0x02,       //       Report Count (2)
    0xA4,             //       Push
    0x81, 0x01,       //         Input (Const,Array,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x05, 0x09,       //         Usage Page (Button)
    0x15, 0x00,       //         Logical Minimum (0)
    0x25, 0x01,       //         Logical Maximum (1)
    0x09, 0x02,       //         Usage (0x02)
    0x95, 0x01,       //         Report Count (1)
    0x81, 0x02,       //         Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0xB4,             //       Pop
    0x81, 0x01,       //       Input (Const,Array,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x05, 0x09,       //       Usage Page (Button)
    0x09, 0x01,       //       Usage (0x01)
    0x95, 0x01,       //       Report Count (1)
    0x15, 0x00,       //       Logical Minimum (0)
    0x25, 0x01,       //       Logical Maximum (1)
    0x81, 0x02,       //       Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x81, 0x01,       //       Input (Const,Array,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0xB4,             //     Pop
    0x09, 0x30,       //     Usage (X)
    0x75, 0x09,       //     Report Size (9)
    0x16, 0x00, 0xFF, //     Logical Minimum (-256)
    0x26, 0xFF, 0x00, //     Logical Maximum (255)
    0x81, 0x06,       //     Input (Data,Var,Rel,No Wrap,Linear,Preferred State,No Null Position)
    0x95, 0x02,       //     Report Count (2)
    0x75, 0x01,       //     Report Size (1)
    0x81, 0x01,       //     Input (Const,Array,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x05, 0x09,       //     Usage Page (Button)
    0x09, 0x03,       //     Usage (0x03)
    0x95, 0x01,       //     Report Count (1)
    0x15, 0x00,       //     Logical Minimum (0)
    0x25, 0x01,       //     Logical Maximum (1)
    0x81, 0x02,       //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x81, 0x01,       //     Input (Const,Array,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0xC0,             //   End Collection
    0xC0,             // End Collection
};

// hid_mouse_3x3x12
// 3-button, 5-byte X, Y, and Wheel coords, 12-bit
// 00000BBB
// XXXXXXXX (lsb)
// YYYYXXXX (lsb of y, msb of x)
// YYYYYYYY (msb)
// WWWWWWWW
static const Bit8u bx_mouse_hid_report_descriptor_3312[] = {
    0x05, 0x01,       // Usage Page (Generic Desktop Ctrls)
    0x09, 0x02,       // Usage (Mouse)
    0xA1, 0x01,       // Collection (Application)
    0x09, 0x01,       //   Usage (Pointer)
    0xA1, 0x00,       //   Collection (Physical)
    0x05, 0x09,       //     Usage Page (Button)
    0x19, 0x01,       //     Usage Minimum (0x01)
    0x29, 0x03,       //     Usage Maximum (0x03)
    0x15, 0x00,       //     Logical Minimum (0)
    0x25, 0x01,       //     Logical Maximum (1)
    0x95, 0x03,       //     Report Count (3)
    0x75, 0x01,       //     Report Size (1)
    0x81, 0x02,       //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x95, 0x01,       //     Report Count (1)
    0x75, 0x05,       //     Report Size (5)
    0x81, 0x01,       //     Input (Const,Array,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x05, 0x01,       //     Usage Page (Generic Desktop Ctrls)
    0x09, 0x30,       //     Usage (X)
    0x09, 0x31,       //     Usage (Y)
    0x95, 0x02,       //     Report Count (2)
    0x16, 0x00, 0xF8, //     Logical Minimum (-2048)
    0x26, 0xFF, 0x07, //     Logical Maximum (2047)
    0x75, 0x0C,       //     Report Size (12)
    0x81, 0x06,       //     Input (Data,Var,Rel,No Wrap,Linear,Preferred State,No Null Position)
    0x09, 0x38,       //     Usage (Wheel)
    0x95, 0x01,       //     Report Count (1)
    0x15, 0x80,       //     Logical Minimum (-128)
    0x25, 0x7F,       //     Logical Maximum (127)
    0x75, 0x08,       //     Report Size (8)
    0x81, 0x06,       //     Input (Data,Var,Rel,No Wrap,Linear,Preferred State,No Null Position)
    0xC0,             //   End Collection
    0xC0,             // End Collection
};

// hid_mouse_3x3x16
// 3-button, 6-byte X, Y, and Wheel coords, 16-bit
// 00000BBB
// XXXXXXXX  (lsb)
// XXXXXXXX  (msb)
// YYYYYYYY  (lsb)
// YYYYYYYY  (msb)
// WWWWWWWW
static const Bit8u bx_mouse_hid_report_descriptor_3316[] = {
    0x05, 0x01,       // Usage Page (Generic Desktop Ctrls)
    0x09, 0x02,       // Usage (Mouse)
    0xA1, 0x01,       // Collection (Application)
    0x09, 0x01,       //   Usage (Pointer)
    0xA1, 0x00,       //   Collection (Physical)
    0x05, 0x09,       //     Usage Page (Button)
    0x19, 0x01,       //     Usage Minimum (0x01)
    0x29, 0x03,       //     Usage Maximum (0x03)
    0x15, 0x00,       //     Logical Minimum (0)
    0x25, 0x01,       //     Logical Maximum (1)
    0x95, 0x03,       //     Report Count (3)
    0x75, 0x01,       //     Report Size (1)
    0x81, 0x02,       //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x95, 0x01,       //     Report Count (1)
    0x75, 0x05,       //     Report Size (5)
    0x81, 0x01,       //     Input (Const,Array,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x05, 0x01,       //     Usage Page (Generic Desktop Ctrls)
    0x09, 0x30,       //     Usage (X)
    0x09, 0x31,       //     Usage (Y)
    0x95, 0x02,       //     Report Count (2)
    0x16, 0x00, 0x80, //     Logical Minimum (-32768)
    0x26, 0xFF, 0x7F, //     Logical Maximum (32767)
    0x75, 0x10,       //     Report Size (16)
    0x81, 0x06,       //     Input (Data,Var,Rel,No Wrap,Linear,Preferred State,No Null Position)
    0x09, 0x38,       //     Usage (Wheel)
    0x95, 0x01,       //     Report Count (1)
    0x15, 0x80,       //     Logical Minimum (-128)
    0x25, 0x7F,       //     Logical Maximum (127)
    0x75, 0x08,       //     Report Size (8)
    0x81, 0x06,       //     Input (Data,Var,Rel,No Wrap,Linear,Preferred State,No Null Position)
    0xC0,             //   End Collection
    0xC0,             // End Collection
};

// This Report Descriptor uses Designator values to point to an entry in the
//  HID Physical Descriptor. WinXP doesn't like the Designator entries, so
//  don't use this model when using a WinXP guest.
// hid_mouse_3x3x8phy
// 3-button, 3-byte X, Y, and Wheel coords, 8-bit
// 00000BBB
// XXXXXXXX
// YYYYYYYY
// WWWWWWWW
static const Bit8u bx_mouse_hid_report_descriptor_338phy[] = {
    0x05, 0x01,           // Usage Page (Generic Desktop Ctrls)
    0x09, 0x02,           // Usage (Mouse)
    0xA1, 0x01,           // Collection (Application)
    0x09, 0x01,           //   Usage (Pointer)
    0x89, HID_CLASS_STR4, // Starting String Index (4)
    0x99, HID_CLASS_STR5, // Ending String Index (5)
    0xA1, 0x00,           //   Collection (Physical)
    0x05, 0x09,           //     Usage Page (Button)
    0x15, 0x00,           //     Logical Minimum (0)
    0x25, 0x01,           //     Logical Maximum (1)
    0x19, 0x01,           //     Usage Minimum (0x01)
    0x29, 0x03,           //     Usage Maximum (0x03)
    0x95, 0x03,           //     Report Count (3)
    0x75, 0x01,           //     Report Size (1)
    0x49, 0x01,           //     Designator Minimum (1)
    0x59, 0x03,           //     Designator Maximum (3)
    0x81, 0x02,           //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x95, 0x01,           //     Report Count (1)
    0x75, 0x05,           //     Report Size (5)
    0x81, 0x01,           //     Input (Const,Array,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x05, 0x01,           //     Usage Page (Generic Desktop Ctrls)
    0x09, 0x30,           //     Usage (X)
    0x09, 0x31,           //     Usage (Y)
    0x09, 0x38,           //     Usage (Wheel)
    0x95, 0x03,           //     Report Count (3)
    0x15, 0x81,           //     Logical Minimum (-127)
    0x25, 0x7F,           //     Logical Maximum (127)
    0x75, 0x08,           //     Report Size (8)
    0x81, 0x06,           //     Input (Data,Var,Rel,No Wrap,Linear,Preferred State,No Null Position)
    0xC0,                 //   End Collection
    0xC0,                 // End Collection
};

// standard low-, full-speed configuration (w/o physical descriptor)
// (this define must be the zero based byte offset of the HID length field below)
#define BX_Mouse_Config_Descriptor0_pos 25
static Bit8u bx_mouse_config_descriptor0[] = {
    /* one configuration */
    0x09,       /*  u8  bLength; */
    0x02,       /*  u8  bDescriptorType; Configuration */
    0x22, 0x00, /*  u16 wTotalLength; */
    0x01,       /*  u8  bNumInterfaces; (1) */
    0x01,       /*  u8  bConfigurationValue; */
    0x04,       /*  u8  iConfiguration; */
    0xa0,       /*  u8  bmAttributes;
                           Bit 7: must be set,
                               6: Self-powered,
                               5: Remote wakeup,
                               4..0: resvd */
    50,         /*  u8  MaxPower; */

    /* one interface */
    0x09, /*  u8  if_bLength; */
    0x04, /*  u8  if_bDescriptorType; Interface */
    0x00, /*  u8  if_bInterfaceNumber; */
    0x00, /*  u8  if_bAlternateSetting; */
    0x01, /*  u8  if_bNumEndpoints; */
    0x03, /*  u8  if_bInterfaceClass; */
    0x01, /*  u8  if_bInterfaceSubClass; */
    0x02, /*  u8  if_bInterfaceProtocol; */
    0x05, /*  u8  if_iInterface; */

    /* HID descriptor */
    0x09,       /*  u8  bLength; */
    0x21,       /*  u8 bDescriptorType; */
    0x01, 0x01, /*  u16 HID_class (0x0101) */
    0x00,       /*  u8 country_code */
    0x01,       /*  u8 num_descriptors */
    0x22,       /*  u8 type; Report */
    0x00, 0x00, /*  u16 len (updated on the fly) */

    /* one endpoint */
    0x07,       /*  u8  ep_bLength; */
    0x05,       /*  u8  ep_bDescriptorType; Endpoint */
    0x81,       /*  u8  ep_bEndpointAddress; IN Endpoint 1 */
    0x03,       /*  u8  ep_bmAttributes; Interrupt */
    0x08, 0x00, /*  u16 ep_wMaxPacketSize; */
    0x0A,       /*  u8  ep_bInterval; (10 - 255ms -- usb 2.0 spec, 5.7.4) */
                /*           however: (11 - 255ms -- usb 2.0 compliance 1.2.84, pg 11 */
};

#define HID_PHYS_DESC_SET_LEN 7
static const Bit8u bx_mouse_phys_descriptor[] = {
    /* HID Physical descriptor */
    /* Descriptor set 0 */
    0x02,                         /*  u8  Number of physical Descriptor sets to follow */
    HID_PHYS_DESC_SET_LEN,        /*  u8  Length of each descriptor set */
    0x00, 0x00, 0x00, 0x00, 0x00, /* pad to HID_PHYS_DESC_SET_LEN bytes */

    /* Descriptor set 1 */
    (HID_PHYS_BIAS_RIGHT_HAND << 5) | 0, /*  u8  bPhysicalInfo; (right hand, 0 = most prefered) */
    IndexFinger, (Right << 5) | 0,       /*  u8*2 DescritorData[0] (index 1) (Index Finger,  right, 0 = easy */
    MiddleFinger, (Right << 5) | 0,      /*  u8*2 DescritorData[1] (index 2) (Middle Finger, right, 0 = easy */
    IndexFinger, (Right << 5) | 0,       /*  u8*2 DescritorData[2] (index 3) (Index Finger,  right, 0 = easy */

    /* Descriptor set 2 */
    (HID_PHYS_BIAS_LEFT_HAND << 5) | 1, /*  u8  bPhysicalInfo; (left hand, 1 = next prefered) */
    MiddleFinger, (Left << 5) | 0,      /*  u8*2 DescritorData[0] (index 1) (Middle Finger, left, 0 = easy */
    IndexFinger, (Left << 5) | 0,       /*  u8*2 DescritorData[1] (index 2) (Index Finger,  left, 0 = easy */
    IndexFinger, (Left << 5) | 0,       /*  u8*2 DescritorData[2] (index 3) (Index Finger,  left, 0 = easy */
};

// standard low-, full-speed configuration (w/ physical descriptor)
// (this define must be the zero based byte offset of the HID length field below)
#define BX_Mouse_Config_Descriptor1_pos 25
static Bit8u bx_mouse_config_descriptor1[] = {
    /* one configuration */
    0x09,       /*  u8  bLength; */
    0x02,       /*  u8  bDescriptorType; Configuration */
    0x25, 0x00, /*  u16 wTotalLength; */
    0x01,       /*  u8  bNumInterfaces; (1) */
    0x01,       /*  u8  bConfigurationValue; */
    0x04,       /*  u8  iConfiguration; */
    0xa0,       /*  u8  bmAttributes;
                           Bit 7: must be set,
                               6: Self-powered,
                               5: Remote wakeup,
                               4..0: resvd */
    50,         /*  u8  MaxPower; */

    /* one interface */
    0x09, /*  u8  if_bLength; */
    0x04, /*  u8  if_bDescriptorType; Interface */
    0x00, /*  u8  if_bInterfaceNumber; */
    0x00, /*  u8  if_bAlternateSetting; */
    0x01, /*  u8  if_bNumEndpoints; */
    0x03, /*  u8  if_bInterfaceClass; */
    0x01, /*  u8  if_bInterfaceSubClass; */
    0x02, /*  u8  if_bInterfaceProtocol; */
    0x05, /*  u8  if_iInterface; */

    /* HID descriptor */
    0x0C,       /*  u8  bLength; */
    0x21,       /*  u8 bDescriptorType; */
    0x01, 0x01, /*  u16 HID_class (0x0101) */
    0x00,       /*  u8 country_code */
    0x02,       /*  u8 num_descriptors */
    0x22,       /*  u8 type; Report */
    0x00, 0x00, /*  u16 len (updated on the fly) */
    0x23,       /*  u8 type; Physical */
    sizeof(bx_mouse_phys_descriptor),
    0x00, /*  u16 len */

    /* one endpoint */
    0x07,       /*  u8  ep_bLength; */
    0x05,       /*  u8  ep_bDescriptorType; Endpoint */
    0x81,       /*  u8  ep_bEndpointAddress; IN Endpoint 1 */
    0x03,       /*  u8  ep_bmAttributes; Interrupt */
    0x08, 0x00, /*  u16 ep_wMaxPacketSize; */
    0x0a,       /*  u8  ep_bInterval; (0 - 255ms -- usb 2.0 spec) */
};

// standard high-speed configuration (w/o physical descriptor)
// (this define must be the zero based byte offset of the HID length field below)
#define BX_Mouse_Config_Descriptor2_pos 25
static Bit8u bx_mouse_config_descriptor2[] = {
    /* one configuration */
    0x09,       /*  u8  bLength; */
    0x02,       /*  u8  bDescriptorType; Configuration */
    0x25, 0x00, /*  u16 wTotalLength; */
    0x01,       /*  u8  bNumInterfaces; (1) */
    0x01,       /*  u8  bConfigurationValue; */
    0x04,       /*  u8  iConfiguration; */
    0xa0,       /*  u8  bmAttributes;
                           Bit 7: must be set,
                               6: Self-powered,
                               5: Remote wakeup,
                               4..0: resvd */
    50,         /*  u8  MaxPower; */

    /* one interface */
    0x09, /*  u8  if_bLength; */
    0x04, /*  u8  if_bDescriptorType; Interface */
    0x00, /*  u8  if_bInterfaceNumber; */
    0x00, /*  u8  if_bAlternateSetting; */
    0x01, /*  u8  if_bNumEndpoints; */
    0x03, /*  u8  if_bInterfaceClass; */
    0x01, /*  u8  if_bInterfaceSubClass; */
    0x02, /*  u8  if_bInterfaceProtocol; */
    0x05, /*  u8  if_iInterface; */

    /* HID descriptor */
    0x09,       /*  u8  bLength; */
    0x21,       /*  u8 bDescriptorType; */
    0x01, 0x01, /*  u16 HID_class (0x0101) */
    0x00,       /*  u8 country_code */
    0x01,       /*  u8 num_descriptors */
    0x22,       /*  u8 type; Report */
    0x00, 0x00, /*  u16 len (updated on the fly) */

    /* one endpoint */
    0x07,       /*  u8  ep_bLength; */
    0x05,       /*  u8  ep_bDescriptorType; Endpoint */
    0x81,       /*  u8  ep_bEndpointAddress; IN Endpoint 1 */
    0x03,       /*  u8  ep_bmAttributes; Interrupt */
    0x08, 0x00, /*  u16 ep_wMaxPacketSize; */
    0x07,       /*  u8  ep_bInterval; (2 ^ (8-1) * 125 usecs = 8 ms) */
};

// standard hid descriptor (w/o physical descriptor)
// (this define must be the zero based byte offset of the HID length field below)
#define BX_Mouse_Hid_Descriptor0 7
static Bit8u bx_mouse_hid_descriptor0[] = {
    /* HID descriptor */
    0x09,       /*  u8  bLength; */
    0x21,       /*  u8 bDescriptorType; */
    0x01, 0x01, /*  u16 HID_class (0x0101) */
    0x00,       /*  u8 country_code */
    0x01,       /*  u8 num_descriptors */
    0x22,       /*  u8 type; Report */
    0x00, 0x00, /*  u16 len (updated on the fly) */
};

// standard hid descriptor (w/ physical descriptor)
// (this define must be the zero based byte offset of the HID length field below)
#define BX_Mouse_Hid_Descriptor1 7
static Bit8u bx_mouse_hid_descriptor1[] = {
    /* HID descriptor */
    0x0C,       /*  u8  bLength; */
    0x21,       /*  u8 bDescriptorType; */
    0x01, 0x01, /*  u16 HID_class (0x0101) */
    0x00,       /*  u8 country_code */
    0x02,       /*  u8 num_descriptors */
    0x22,       /*  u8 type; Report */
    0x00, 0x00, /*  u16 len (updated on the fly) */
    0x23,       /*  u8 type; Physical */
    sizeof(bx_mouse_phys_descriptor),
    0x00, /*  u16 len */
};

////////////////////////////////////////////////
// tablet
static const Bit8u bx_tablet_hid_report_descriptor[] = {
    0x05, 0x01,       // Usage Page (Generic Desktop Ctrls)
    0x09, 0x02,       // Usage (Mouse)
    0xA1, 0x01,       // Collection (Application)
    0x09, 0x01,       //   Usage (Pointer)
    0xA1, 0x00,       //   Collection (Physical)
    0x05, 0x09,       //     Usage Page (Button)
    0x19, 0x01,       //     Usage Minimum (0x01)
    0x29, 0x03,       //     Usage Maximum (0x03)
    0x15, 0x00,       //     Logical Minimum (0)
    0x25, 0x01,       //     Logical Maximum (1)
    0x95, 0x03,       //     Report Count (3)
    0x75, 0x01,       //     Report Size (1)
    0x81, 0x02,       //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x95, 0x01,       //     Report Count (1)
    0x75, 0x05,       //     Report Size (5)
    0x81, 0x01,       //     Input (Const,Array,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x05, 0x01,       //     Usage Page (Generic Desktop Ctrls)
    0x09, 0x30,       //     Usage (X)
    0x09, 0x31,       //     Usage (Y)
    0x15, 0x00,       //     Logical Minimum (0)
    0x26, 0xFF, 0x7F, //     Logical Maximum (32767)
    0x35, 0x00,       //     Physical Minimum (0)
    0x46, 0xFF, 0x7F, //     Physical Maximum (32767)
    0x75, 0x10,       //     Report Size (16)
    0x95, 0x02,       //     Report Count (2)
    0x81, 0x02,       //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x05, 0x01,       //     Usage Page (Generic Desktop Ctrls)
    0x09, 0x38,       //     Usage (Wheel)
    0x15, 0x81,       //     Logical Minimum (-127)
    0x25, 0x7F,       //     Logical Maximum (127)
    0x35, 0x00,       //     Physical Minimum (0)
    0x45, 0x00,       //     Physical Maximum (0)
    0x75, 0x08,       //     Report Size (8)
    0x95, 0x01,       //     Report Count (1)
    0x81, 0x06,       //     Input (Data,Var,Rel,No Wrap,Linear,Preferred State,No Null Position)
    0xC0,             //   End Collection
    0xC0,             // End Collection
};

static const Bit8u bx_tablet_config_descriptor[] = {
    /* one configuration */
    0x09,       /*  u8  bLength; */
    0x02,       /*  u8  bDescriptorType; Configuration */
    0x22, 0x00, /*  u16 wTotalLength; */
    0x01,       /*  u8  bNumInterfaces; (1) */
    0x01,       /*  u8  bConfigurationValue; */
    0x04,       /*  u8  iConfiguration; */
    0xa0,       /*  u8  bmAttributes;
                           Bit 7: must be set,
                               6: Self-powered,
                               5: Remote wakeup,
                               4..0: resvd */
    50,         /*  u8  MaxPower; */

    /* one interface */
    0x09, /*  u8  if_bLength; */
    0x04, /*  u8  if_bDescriptorType; Interface */
    0x00, /*  u8  if_bInterfaceNumber; */
    0x00, /*  u8  if_bAlternateSetting; */
    0x01, /*  u8  if_bNumEndpoints; */
    0x03, /*  u8  if_bInterfaceClass; */
    0x01, /*  u8  if_bInterfaceSubClass; */
    0x02, /*  u8  if_bInterfaceProtocol; */
    0x05, /*  u8  if_iInterface; */

    /* HID descriptor */
    0x09,       /*  u8  bLength; */
    0x21,       /*  u8 bDescriptorType; */
    0x01, 0x01, /*  u16 HID_class (0x0101) */
    0x00,       /*  u8 country_code */
    0x01,       /*  u8 num_descriptors */
    0x22,       /*  u8 type; Report */
    sizeof(bx_tablet_hid_report_descriptor),
    0x00, /*  u16 len */

    /* one endpoint */
    0x07,       /*  u8  ep_bLength; */
    0x05,       /*  u8  ep_bDescriptorType; Endpoint */
    0x81,       /*  u8  ep_bEndpointAddress; IN Endpoint 1 */
    0x03,       /*  u8  ep_bmAttributes; Interrupt */
    0x06, 0x00, /*  u16 ep_wMaxPacketSize; */
    0x0a,       /*  u8  ep_bInterval; (0 - 255ms -- usb 2.0 spec) */
};

static const Bit8u bx_tablet_config_descriptor2[] = {
    /* one configuration */
    0x09,       /*  u8  bLength; */
    0x02,       /*  u8  bDescriptorType; Configuration */
    0x22, 0x00, /*  u16 wTotalLength; */
    0x01,       /*  u8  bNumInterfaces; (1) */
    0x01,       /*  u8  bConfigurationValue; */
    0x04,       /*  u8  iConfiguration; */
    0xa0,       /*  u8  bmAttributes;
                           Bit 7: must be set,
                               6: Self-powered,
                               5: Remote wakeup,
                               4..0: resvd */
    50,         /*  u8  MaxPower; */

    /* one interface */
    0x09, /*  u8  if_bLength; */
    0x04, /*  u8  if_bDescriptorType; Interface */
    0x00, /*  u8  if_bInterfaceNumber; */
    0x00, /*  u8  if_bAlternateSetting; */
    0x01, /*  u8  if_bNumEndpoints; */
    0x03, /*  u8  if_bInterfaceClass; */
    0x01, /*  u8  if_bInterfaceSubClass; */
    0x02, /*  u8  if_bInterfaceProtocol; */
    0x05, /*  u8  if_iInterface; */

    /* HID descriptor */
    0x09,       /*  u8  bLength; */
    0x21,       /*  u8 bDescriptorType; */
    0x01, 0x01, /*  u16 HID_class (0x0101) */
    0x00,       /*  u8 country_code */
    0x01,       /*  u8 num_descriptors */
    0x22,       /*  u8 type; Report */
    sizeof(bx_tablet_hid_report_descriptor),
    0x00, /*  u16 len */

    /* one endpoint */
    0x07,       /*  u8  ep_bLength; */
    0x05,       /*  u8  ep_bDescriptorType; Endpoint */
    0x81,       /*  u8  ep_bEndpointAddress; IN Endpoint 1 */
    0x03,       /*  u8  ep_bmAttributes; Interrupt */
    0x06, 0x00, /*  u16 ep_wMaxPacketSize; */
    0x04,       /*  u8  ep_bInterval; (2 ^ (4-1) * 125 usecs = 1 ms) */
};

static const Bit8u bx_tablet_hid_descriptor[] = {
    /* HID descriptor */
    0x09,       /*  u8  bLength; */
    0x21,       /*  u8 bDescriptorType; */
    0x01, 0x01, /*  u16 HID_class (0x0101) */
    0x00,       /*  u8 country_code */
    0x01,       /*  u8 num_descriptors */
    0x22,       /*  u8 type; Report */
    sizeof(bx_tablet_hid_report_descriptor),
    0x00, /*  u16 len */
};

////////////////////////////////////////////////
// keyboard/keypad
static const Bit8u bx_keypad_hid_report_descriptor[] = {
    0x05, 0x01,       // Usage Page (Generic Desktop Ctrls)
    0x09, 0x06,       // Usage (Keyboard)
    0xA1, 0x01,       // Collection (Application)
    0x05, 0x07,       //   Usage Page (Kbrd/Keypad)
    0x19, 0xE0,       //   Usage Minimum (Keyboard Left Control)
    0x29, 0xE7,       //   Usage Maximum (Keyboard Right GUI)
    0x15, 0x00,       //   Logical Minimum (0)
    0x25, 0x01,       //   Logical Maximum (1)
    0x75, 0x01,       //   Report Size (1)
    0x95, 0x08,       //   Report Count (8)
    0x81, 0x02,       //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x95, 0x01,       //   Report Count (1)
    0x75, 0x08,       //   Report Size (8)
    0x81, 0x01,       //   Input (Const,Array,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x95, 0x03,       //   Report Count (3)
    0x75, 0x01,       //   Report Size (1)
    0x05, 0x08,       //   Usage Page (LEDs)
    0x19, 0x01,       //   Usage Minimum (Num Lock)
    0x29, 0x03,       //   Usage Maximum (Scroll Lock)
    0x91, 0x02,       //   Output (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
    0x95, 0x05,       //   Report Count (5)
    0x75, 0x01,       //   Report Size (1)
    0x91, 0x01,       //   Output (Const,Array,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
    0x95, 0x06,       //   Report Count (6)
    0x75, 0x08,       //   Report Size (8)
    0x15, 0x00,       //   Logical Minimum (0)
    0x26, 0xFF, 0x00, //   Logical Maximum (255)
    0x05, 0x07,       //   Usage Page (Kbrd/Keypad)
    0x19, 0x00,       //   Usage Minimum (0)
    0x29, 0xE7,       //   Usage Maximum (Keyboard Right GUI)
    0x81, 0x00,       //   Input (Data,Array,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0xC0,             // End Collection
};

static const Bit8u bx_keypad_dev_descriptor[] = {
    0x12,       /*  u8 bLength; */
    0x01,       /*  u8 bDescriptorType; Device */
    0x01, 0x01, /*  u16 bcdUSB; v1.1 */

    0x00, /*  u8  bDeviceClass; */
    0x00, /*  u8  bDeviceSubClass; */
    0x00, /*  u8  bDeviceProtocol; */
    0x08, /*  u8  bMaxPacketSize; 8 Bytes */

    0xB4, 0x04, /*  u16 idVendor; */
    0x01, 0x01, /*  u16 idProduct; */
    0x01, 0x00, /*  u16 bcdDevice */

    0x01, /*  u8  iManufacturer; */
    0x02, /*  u8  iProduct; */
    0x03, /*  u8  iSerialNumber; */
    0x01  /*  u8  bNumConfigurations; */
};

static const Bit8u bx_keypad_dev_descriptor2[] = {
    0x12,       /*  u8 bLength; */
    0x01,       /*  u8 bDescriptorType; Device */
    0x00, 0x02, /*  u16 bcdUSB; v2.0 */

    0x00, /*  u8  bDeviceClass; */
    0x00, /*  u8  bDeviceSubClass; */
    0x00, /*  u8  bDeviceProtocol; */
    0x40, /*  u8  bMaxPacketSize; 64 Bytes */

    0xB4, 0x04, /*  u16 idVendor; */
    0x01, 0x01, /*  u16 idProduct; */
    0x01, 0x00, /*  u16 bcdDevice */

    0x01, /*  u8  iManufacturer; */
    0x02, /*  u8  iProduct; */
    0x03, /*  u8  iSerialNumber; */
    0x01  /*  u8  bNumConfigurations; */
};

static const Bit8u bx_keypad_config_descriptor[] = {
    /* one configuration */
    0x09,       /*  u8  bLength; */
    0x02,       /*  u8  bDescriptorType; Configuration */
    0x22, 0x00, /*  u16 wTotalLength; */
    0x01,       /*  u8  bNumInterfaces; (1) */
    0x01,       /*  u8  bConfigurationValue; */
    0x04,       /*  u8  iConfiguration; */
    0xa0,       /*  u8  bmAttributes;
                           Bit 7: must be set,
                               6: Self-powered,
                               5: Remote wakeup,
                               4..0: resvd */
    50,         /*  u8  MaxPower; */

    /* one interface */
    0x09, /*  u8  if_bLength; */
    0x04, /*  u8  if_bDescriptorType; Interface */
    0x00, /*  u8  if_bInterfaceNumber; */
    0x00, /*  u8  if_bAlternateSetting; */
    0x01, /*  u8  if_bNumEndpoints; */
    0x03, /*  u8  if_bInterfaceClass; */
    0x01, /*  u8  if_bInterfaceSubClass; */
    0x01, /*  u8  if_bInterfaceProtocol; */
    0x05, /*  u8  if_iInterface; */

    /* HID descriptor */
    0x09,       /*  u8  bLength; */
    0x21,       /*  u8 bDescriptorType; */
    0x01, 0x01, /*  u16 HID_class (0x0101) */
    0x00,       /*  u8 country_code */
    0x01,       /*  u8 num_descriptors */
    0x22,       /*  u8 type; Report */
    sizeof(bx_keypad_hid_report_descriptor),
    0x00, /*  u16 len */

    /* one endpoint (status change endpoint) */
    0x07,       /*  u8  ep_bLength; */
    0x05,       /*  u8  ep_bDescriptorType; Endpoint */
    0x81,       /*  u8  ep_bEndpointAddress; IN Endpoint 1 */
    0x03,       /*  u8  ep_bmAttributes; Interrupt */
    0x08, 0x00, /*  u16 ep_wMaxPacketSize; */
    0x0a,       /*  u8  ep_bInterval; (255ms -- usb 2.0 spec) */
};

static const Bit8u bx_keypad_config_descriptor2[] = {
    /* one configuration */
    0x09,       /*  u8  bLength; */
    0x02,       /*  u8  bDescriptorType; Configuration */
    0x22, 0x00, /*  u16 wTotalLength; */
    0x01,       /*  u8  bNumInterfaces; (1) */
    0x01,       /*  u8  bConfigurationValue; */
    0x04,       /*  u8  iConfiguration; */
    0xa0,       /*  u8  bmAttributes;
                           Bit 7: must be set,
                               6: Self-powered,
                               5: Remote wakeup,
                               4..0: resvd */
    50,         /*  u8  MaxPower; */

    /* one interface */
    0x09, /*  u8  if_bLength; */
    0x04, /*  u8  if_bDescriptorType; Interface */
    0x00, /*  u8  if_bInterfaceNumber; */
    0x00, /*  u8  if_bAlternateSetting; */
    0x01, /*  u8  if_bNumEndpoints; */
    0x03, /*  u8  if_bInterfaceClass; */
    0x01, /*  u8  if_bInterfaceSubClass; */
    0x01, /*  u8  if_bInterfaceProtocol; */
    0x05, /*  u8  if_iInterface; */

    /* HID descriptor */
    0x09,       /*  u8  bLength; */
    0x21,       /*  u8 bDescriptorType; */
    0x01, 0x01, /*  u16 HID_class (0x0101) */
    0x00,       /*  u8 country_code */
    0x01,       /*  u8 num_descriptors */
    0x22,       /*  u8 type; Report */
    sizeof(bx_keypad_hid_report_descriptor),
    0x00, /*  u16 len */

    /* one endpoint (status change endpoint) */
    0x07,       /*  u8  ep_bLength; */
    0x05,       /*  u8  ep_bDescriptorType; Endpoint */
    0x81,       /*  u8  ep_bEndpointAddress; IN Endpoint 1 */
    0x03,       /*  u8  ep_bmAttributes; Interrupt */
    0x08, 0x00, /*  u16 ep_wMaxPacketSize; */
    0x07,       /*  u8  ep_bInterval; (2 ^ (8-1) * 125 usecs = 8 ms) */
};

static const Bit8u bx_keypad_hid_descriptor[] = {
    /* HID descriptor */
    0x09,       /*  u8  bLength; */
    0x21,       /*  u8 bDescriptorType; */
    0x01, 0x01, /*  u16 HID_class (0x0101) */
    0x00,       /*  u8 country_code */
    0x01,       /*  u8 num_descriptors */
    0x22,       /*  u8 type; Report */
    sizeof(bx_keypad_hid_report_descriptor),
    0x00, /*  u16 len */
};

#define BX_ERROR(x) pclog x
#define BX_INFO(x)  pclog x
#define BX_DEBUG(x) pclog x
#define BX_PANIC(x) fatal x

int
usb_device_hid_create_mouse_packet(usb_device_hid *hid, Bit8u *buf)
{
    int l = 0;

    // The HID Boot Protocol report is only three bytes long
    if (hid->s.boot_protocol == PROTOCOL_BOOT) {
        buf[0] = (Bit8u) hid->s.b_state;
        buf[1] = (Bit8s) hid->s.mouse_x;
        buf[2] = (Bit8s) hid->s.mouse_y;
        l      = 3;
    } else { // do the Report Protocol

        // do we add a Report ID field?
        if (hid->s.report_id > 0) {
            *buf++ = hid->s.report_id;
            l      = 1;
        }

        if (hid->device.type == USB_HID_TYPE_TABLET) {
            *buf++ = (Bit8u) hid->s.b_state;
            *buf++ = (Bit8u) (hid->s.mouse_x & 0xff);
            *buf++ = (Bit8u) (hid->s.mouse_x >> 8);
            *buf++ = (Bit8u) (hid->s.mouse_y & 0xff);
            *buf++ = (Bit8u) (hid->s.mouse_y >> 8);
            *buf++ = (Bit8s) hid->s.mouse_z;
            l += 6;
        } else {
            // USB_HID_TYPE_MOUSE
            switch (hid->s.model) {
                // default 2-button, X and Y coords, 8-bit
                // 000000BB
                // XXXXXXXX
                // YYYYYYYY
                case hid_mouse_2x2x8:
                    *buf++ = (hid->s.b_state & 3);
                    *buf++ = (Bit8s) hid->s.mouse_x;
                    *buf++ = (Bit8s) hid->s.mouse_y;
                    l += 3;
                    break;

                // 3-button, X, Y, and Wheel coords, 8-bit
                // 00000BBB
                // XXXXXXXX
                // YYYYYYYY
                // WWWWWWWW
                case hid_mouse_3x3x8:
                case hid_mouse_3x3x8phy:
                    *buf++ = hid->s.b_state & 7;
                    *buf++ = (Bit8s) hid->s.mouse_x;
                    *buf++ = (Bit8s) hid->s.mouse_y;
                    *buf++ = (Bit8s) hid->s.mouse_z;
                    l += 4;
                    break;

                // 3-button, 5-byte X, Y, and Wheel coords (debug model)
                // YYYYYYY0 - 10 bit Y displacement
                // WWWW0YYY - 8 bit W displacement
                // 0B00WWWW - bit 6 is Button #2 (right button)
                // XXXXX0B0 - 9 bit X displacement, bit 1 is Button #1 (left button)
                // 0B00XXXX - bit 6 is Button #3 (middle button)
                case hid_mouse_3x3xDebug:
                    *buf++ = (Bit8u) (((Bit16u) hid->s.mouse_y & 0x7F) << 1);
                    *buf++ = (Bit8u) ((((Bit16u) hid->s.mouse_y >> 7) & 0x07) | (((Bit16u) hid->s.mouse_z & 0x0F) << 4));
                    *buf++ = (Bit8u) ((((Bit16u) hid->s.mouse_z >> 4) & 0x0F) | (((hid->s.b_state & 2) >> 1) << 6));
                    *buf++ = (Bit8u) ((((hid->s.b_state & 1) >> 0) << 1) | (((Bit16u) hid->s.mouse_x & 0x1F) << 3));
                    *buf++ = (Bit8u) ((((Bit16u) hid->s.mouse_x >> 5) & 0x0F) | (((hid->s.b_state & 4) >> 2) << 6));
                    l += 5;
                    break;

                // 3-button, X, Y, and Wheel coords, 12-bit
                // 00000BBB
                // XXXXXXXX (lsb)
                // YYYYXXXX (lsb of y, msb of x)
                // YYYYYYYY (msb)
                // WWWWWWWW
                case hid_mouse_3x3x12:
                    *buf++ = hid->s.b_state & 7;
                    *buf++ = (Bit8u) ((Bit16u) hid->s.mouse_x & 0xFF);
                    *buf++ = (Bit8u) ((((Bit16u) hid->s.mouse_x >> 8) & 0x0F) | (((Bit16u) hid->s.mouse_y & 0x0F) << 4));
                    *buf++ = (Bit8u) (((Bit16u) hid->s.mouse_y >> 4) & 0xFF);
                    *buf++ = (Bit8s) hid->s.mouse_z;
                    l += 5;
                    break;

                // 3-button, X, Y, and Wheel coords, 16-bit
                // 00000BBB
                // XXXXXXXX  (lsb)
                // XXXXXXXX  (msb)
                // YYYYYYYY  (lsb)
                // YYYYYYYY  (msb)
                // WWWWWWWW
                case hid_mouse_3x3x16:
                    *buf++ = hid->s.b_state & 7;
                    *buf++ = (Bit8u) ((Bit16u) hid->s.mouse_x & 0xFF);
                    *buf++ = (Bit8u) ((Bit16u) hid->s.mouse_x >> 8) & 0xFF;
                    *buf++ = (Bit8u) ((Bit16u) hid->s.mouse_y & 0xFF);
                    *buf++ = (Bit8u) ((Bit16u) hid->s.mouse_y >> 8) & 0xFF;
                    *buf++ = (Bit8s) hid->s.mouse_z;
                    l += 6;
                    break;
            }
            hid->s.mouse_x = 0;
            hid->s.mouse_y = 0;
            hid->s.mouse_z = 0;
        }
    }

    return l;
}

void
mouse_enq(usb_device_hid *hid, int delta_x, int delta_y, int delta_z, unsigned button_state, bool absxy)
{
    Bit16s prev_x, prev_y;

    if (hid->device.type == USB_HID_TYPE_MOUSE) {
        // scale down the motion
        if ((delta_x < -1) || (delta_x > 1))
            delta_x /= 2;
        if ((delta_y < -1) || (delta_y > 1))
            delta_y /= 2;

        if (delta_x > 127)
            delta_x = 127;
        if (delta_y > 127)
            delta_y = 127;
        if (delta_x < -128)
            delta_x = -128;
        if (delta_y < -128)
            delta_y = -128;

        hid->s.mouse_delayed_dx += delta_x;
        hid->s.mouse_delayed_dy -= delta_y;

        if (hid->s.mouse_delayed_dx > 127) {
            delta_x = 127;
            hid->s.mouse_delayed_dx -= 127;
        } else if (hid->s.mouse_delayed_dx < -128) {
            delta_x = -128;
            hid->s.mouse_delayed_dx += 128;
        } else {
            delta_x                 = hid->s.mouse_delayed_dx;
            hid->s.mouse_delayed_dx = 0;
        }
        if (hid->s.mouse_delayed_dy > 127) {
            delta_y = 127;
            hid->s.mouse_delayed_dy -= 127;
        } else if (hid->s.mouse_delayed_dy < -128) {
            delta_y = -128;
            hid->s.mouse_delayed_dy += 128;
        } else {
            delta_y                 = hid->s.mouse_delayed_dy;
            hid->s.mouse_delayed_dy = 0;
        }

        hid->s.mouse_x = (Bit8s) delta_x;
        hid->s.mouse_y = (Bit8s) delta_y;
        hid->s.mouse_z = (Bit8s) delta_z;
        if ((hid->s.mouse_x != 0) || (hid->s.mouse_y != 0) || (hid->s.mouse_z != 0) || (button_state != hid->s.b_state)) {
            hid->s.b_state = (Bit8u) button_state;
            if (hid->s.mouse_event_count < BX_KBD_ELEMENTS) {
                hid->s.mouse_event_buf_len[hid->s.mouse_event_count] = usb_device_hid_create_mouse_packet(hid, hid->s.mouse_event_buf[hid->s.mouse_event_count]);
                hid->s.mouse_event_count++;
            }
            hid->s.has_events = 1;
        }
    }
}

int
usb_device_hid_get_mouse_packet(usb_device_hid *hid, Bit8u *buf)
{
    int l = USB_RET_NAK;

    if (hid->s.mouse_event_count > 0) {
        memcpy(buf, hid->s.mouse_event_buf[0], hid->s.mouse_event_buf_len[0]);
        l = hid->s.mouse_event_buf_len[0];
        if (--hid->s.mouse_event_count > 0) {
            memmove(hid->s.mouse_event_buf[0], hid->s.mouse_event_buf[1],
                    hid->s.mouse_event_count * BX_M_ELEMENTS_SIZE);
            memmove(&hid->s.mouse_event_buf_len[0], &hid->s.mouse_event_buf_len[1],
                    hid->s.mouse_event_count * sizeof(hid->s.mouse_event_buf_len[0]));
        }
    }

    return l;
}

void
usb_device_hid_idle_timer(void *priv)
{
    usb_device_hid *hid = (usb_device_hid *) priv;

    hid->s.has_events = 1;
}

void
usb_device_hid_start_timer(usb_device_hid *hid)
{
    if (hid->s.idle > 0)
        timer_on_auto(&hid->idle_timer, HID_IDLE_TIME * hid->s.idle);
}

int
usb_mouse_poll(usb_device_hid *hid, Bit8u *buf, bool force)
{
    int l = USB_RET_NAK;

    if (hid->device.type == USB_HID_TYPE_MOUSE) {
        if (!hid->s.has_events) {
            // if there's no new movement, handle delayed one
            mouse_enq(hid, 0, 0, hid->s.mouse_z, hid->s.b_state, 0);
        }
        if (hid->s.has_events || force) {
            if (hid->s.mouse_event_count > 0) {
                l = usb_device_hid_get_mouse_packet(hid, buf);
            } else {
                l = usb_device_hid_create_mouse_packet(hid, buf);
            }
            hid->s.has_events = (hid->s.mouse_event_count > 0);
            usb_device_hid_start_timer(hid);
        }
    }
    return l;
}

int
usb_device_hid_handle_control(usb_device_c *device, int request, int value, int index, int length, Bit8u *data)
{
    int             ret = 0;
    usb_device_hid *hid = (usb_device_hid *) device;

    ret = usb_device_handle_control_common(device, request, value, index, length, data);
    if (ret >= 0) {
        return ret;
    }

    ret = 0;

    switch (request) {
        case DeviceOutRequest | USB_REQ_CLEAR_FEATURE:
            BX_DEBUG(("HID: DeviceRequest | CLEAR_FEATURE:"));
            goto fail;
            break;
        case DeviceOutRequest | USB_REQ_SET_FEATURE:
            BX_DEBUG(("HID: DeviceRequest | SET_FEATURE:"));
            goto fail;
            break;
        case EndpointRequest | USB_REQ_GET_STATUS:
            BX_DEBUG(("USB_REQ_GET_STATUS: Endpoint."));
            // if the endpoint is currently halted, return bit 0 = 1
            if (value == USB_ENDPOINT_HALT) {
                if (index == 0x81) {
                    data[0] = 0x00 | (usb_device_get_halted(device, index) ? 1 : 0);
                    data[1] = 0x00;
                    ret     = 2;
                } else {
                    BX_ERROR(("EndpointRequest | USB_REQ_GET_STATUS: index > ep count: %d", index));
                    goto fail;
                }
            } else {
                BX_ERROR(("EndpointRequest | USB_REQ_SET_FEATURE: Unknown Get Status Request found: %d", value));
                goto fail;
            }
            break;
        case DeviceRequest | USB_REQ_GET_DESCRIPTOR:
            BX_DEBUG(("HID: DeviceRequest | USB_REQ_GET_DESCRIPTOR:"));
            switch (value >> 8) {
                case USB_DT_STRING:
                    switch (value & 0xff) {
                        case HID_CLASS_STR4:
                            ret = usb_set_usb_string(data, "HID Mouse");
                            break;
                        case HID_CLASS_STR5:
                            ret = usb_set_usb_string(data, "Endpoint1 Interrupt Pipe");
                            break;
                        default:
                            BX_ERROR(("USB HID handle_control: unknown string descriptor 0x%02x", value & 0xff));
                            goto fail;
                    }
                    break;
                default:
                    BX_ERROR(("USB HID handle_control: unknown descriptor type 0x%02x", value >> 8));
                    goto fail;
            }
            break;
            /* hid specific requests */
        case InterfaceRequest | USB_REQ_GET_DESCRIPTOR:
            BX_DEBUG(("HID: InterfaceRequest | USB_REQ_GET_DESCRIPTOR:"));
            switch (value >> 8) {
                case 0x21: // HID Descriptor
                    if ((value & 0xFF) != 0) {
                        BX_ERROR(("USB_REQ_GET_DESCRIPTOR: The Descriptor Index must be zero for this request."));
                    }
                    if (device->type == USB_HID_TYPE_MOUSE) {
                        if (hid->s.model >= hid_mouse_3x3x8phy) {
                            memcpy(data, bx_mouse_hid_descriptor1,
                                   sizeof(bx_mouse_hid_descriptor1));
                            ret = sizeof(bx_mouse_hid_descriptor1);
                        } else {
                            memcpy(data, bx_mouse_hid_descriptor0,
                                   sizeof(bx_mouse_hid_descriptor0));
                            ret = sizeof(bx_mouse_hid_descriptor0);
                        }
                    } else if (device->type == USB_HID_TYPE_TABLET) {
                        memcpy(data, bx_tablet_hid_descriptor,
                               sizeof(bx_tablet_hid_descriptor));
                        ret = sizeof(bx_tablet_hid_descriptor);
                    } else if ((device->type == USB_HID_TYPE_KEYPAD) || (device->type == USB_HID_TYPE_KEYBOARD)) {
                        memcpy(data, bx_keypad_hid_descriptor,
                               sizeof(bx_keypad_hid_descriptor));
                        ret = sizeof(bx_keypad_hid_descriptor);
                    } else {
                        goto fail;
                    }
                    break;
                case 0x22: // HID Report Descriptor
                    if ((value & 0xFF) != 0) {
                        BX_ERROR(("USB HID handle_control: The Descriptor Index must be zero for this request."));
                    }
                    if (device->type == USB_HID_TYPE_MOUSE) {
                        memcpy(data, hid->s.bx_mouse_hid_report_descriptor, hid->s.bx_mouse_hid_report_descriptor_len);
                        ret = hid->s.bx_mouse_hid_report_descriptor_len;
                    } else if (device->type == USB_HID_TYPE_TABLET) {
                        memcpy(data, bx_tablet_hid_report_descriptor,
                               sizeof(bx_tablet_hid_report_descriptor));
                        ret = sizeof(bx_tablet_hid_report_descriptor);
                    } else if ((device->type == USB_HID_TYPE_KEYPAD) || (device->type == USB_HID_TYPE_KEYBOARD)) {
                        memcpy(data, bx_keypad_hid_report_descriptor,
                               sizeof(bx_keypad_hid_report_descriptor));
                        ret = sizeof(bx_keypad_hid_report_descriptor);
                    } else {
                        goto fail;
                    }
                    // now the guest knows the report id, so we need to use it
                    hid->s.report_id = hid->s.report_use_id;
                    break;
                case 0x23: // HID Physical Descriptor
                    BX_ERROR(("USB HID handle_control: Host requested the HID Physical Descriptor"));
                    if (device->type == USB_HID_TYPE_MOUSE) {
                        int set = (value & 0xFF);
                        if ((set >= 0) && (set <= 2)) {
                            memcpy(data, bx_mouse_phys_descriptor + (set * HID_PHYS_DESC_SET_LEN), HID_PHYS_DESC_SET_LEN);
                            ret = HID_PHYS_DESC_SET_LEN;
                        } else {
                            goto fail;
                        }
                    } else {
                        goto fail;
                    }
                    break;
                default: // 0x24 -> 0x2F
                    BX_ERROR(("USB HID handle_control: unknown HID descriptor 0x%02x", value >> 8));
                    goto fail;
            }
            break;
        case InterfaceInClassRequest | GET_REPORT:
            BX_DEBUG(("HID: GET_REPORT:"));
            if ((value >> 8) == 1) { // Input report
                if ((value & 0xFF) == hid->s.report_id) {
                    if ((device->type == USB_HID_TYPE_MOUSE) || (device->type == USB_HID_TYPE_TABLET)) {
                        ret = usb_mouse_poll(hid, data, 1);
                        if (ret > length)
                            ret = length;
                    } else {
                        goto fail;
                    }
                } else {
                    BX_ERROR(("USB HID handle_control: Report ID (%d) doesn't match requested ID (%d)", hid->s.report_id, value & 0xFF));
                    goto fail;
                }
            } else {
                BX_ERROR(("USB HID handle_control: Requested report type (%d) must be Input(1)", (value >> 8) & 0xFF));
                goto fail;
            }
            break;
        case InterfaceOutClassRequest | SET_REPORT:
            BX_DEBUG(("HID: SET_REPORT:"));
            {
                goto fail;
            }
            break;
        case InterfaceInClassRequest | GET_IDLE:
            BX_DEBUG(("HID: GET_IDLE:"));
            // The wLength field should be 1 for this request
            if (length != 1) {
                BX_ERROR(("USB HID handle_control: The wLength field should be 1 for this request."));
            }
            if ((value & 0xFF00) != 0) {
                BX_ERROR(("USB HID handle_control: High byte of Value must be 0."));
            }
            if ((value & 0xFF) == hid->s.report_id) {
                data[0] = hid->s.idle;
                ret     = 1;
            } else {
                BX_ERROR(("USB HID handle_control: Report ID (%d) doesn't match requested ID (%d)", hid->s.report_id, value & 0xFF));
                goto fail;
            }
            break;
        case InterfaceOutClassRequest | SET_IDLE:
            BX_DEBUG(("HID: SET_IDLE:"));
            // The wLength field should be 0 for this request
            if (length != 0) {
                BX_ERROR(("USB HID handle_control: The wLength field should be 0 for this request."));
            }
            if ((value & 0xFF) == hid->s.report_id) {
                hid->s.idle = (value >> 8);
                usb_device_hid_start_timer(hid);
                ret = 0;
            } else {
                BX_ERROR(("USB HID handle_control: Report ID (%d) doesn't match requested ID (%d)", hid->s.report_id, value & 0xFF));
                goto fail;
            }
            break;
        case InterfaceOutClassRequest | SET_PROTOCOL:
            BX_DEBUG(("HID: SET_PROTOCOL:"));
            // The wLength field should be 0 for this request
            if (length != 0) {
                BX_ERROR(("HID SET_PROTOCOL: The wLength field should be 0 for this request."));
            }
            if ((value != 0) && (value != 1)) {
                BX_ERROR(("HID SET_PROTOCOL: The wValue field must be 0 or 1 for this request."));
            }
            if (value == 0) {
                BX_DEBUG(("HID SET_PROTOCOL: SET_PROTOCOL: Boot Protocol"));
                hid->s.boot_protocol = PROTOCOL_BOOT;
                ret                  = 0;
            } else if (value == 1) {
                BX_DEBUG(("HID SET_PROTOCOL: SET_PROTOCOL: Report Protocol"));
                hid->s.boot_protocol = PROTOCOL_REPORT;
                ret                  = 0;
            } else
                goto fail;
            break;
        case InterfaceInClassRequest | GET_PROTOCOL:
            BX_DEBUG(("HID: GET_PROTOCOL:"));
            // The wLength field should be 1 for this request
            if (length != 1) {
                BX_ERROR(("HID GET_PROTOCOL: The wLength field should be 1 for this request."));
            }
            data[0] = (hid->s.boot_protocol == PROTOCOL_BOOT) ? 0 : 1;
            ret     = 1;
            break;
        default:
            BX_ERROR(("USB HID handle_control: unknown request 0x%04x", request));
fail:
            device->stall = 1;
            ret           = USB_RET_STALL;
            break;
    }
    return ret;
}

int
usb_device_hid_handle_data(usb_device_c *device, USBPacket *p)
{
    int ret = 0;

    // check that the length is <= the max packet size of the device
    if (p->len > usb_device_get_mps(device, p->devep)) {
        BX_DEBUG(("EP%d transfer length (%d) is greater than Max Packet Size (%d).", p->devep, p->len, usb_device_get_mps(device, (p->devep))));
    }

    switch (p->pid) {
        case USB_TOKEN_IN:
            if (p->devep == 1) {
                if ((device->type == USB_HID_TYPE_MOUSE) || (device->type == USB_HID_TYPE_TABLET)) {
                    ret = usb_mouse_poll((usb_device_hid *) device, p->data, 0);
                } else {
                    goto fail;
                }
            } else {
                goto fail;
            }
            break;
        case USB_TOKEN_OUT:
            BX_ERROR(("USB HID handle_data: unexpected pid TOKEN_OUT"));
        default:
fail:
            device->stall = 1;
            ret           = USB_RET_STALL;
            break;
    }

    return ret;
}

bool
usb_hid_device_init(usb_device_c* device)
{
    usb_device_hid *hid = (usb_device_hid *) device;
    if ((hid->device.type == USB_HID_TYPE_MOUSE) || (hid->device.type == USB_HID_TYPE_TABLET)) {
        if (hid->device.speed == USB_SPEED_HIGH) {
            hid->device.dev_descriptor                                = bx_mouse_dev_descriptor2;
            hid->device.device_desc_size                              = sizeof(bx_mouse_dev_descriptor2);
            hid->device.endpoint_info[USB_CONTROL_EP].max_packet_size = 64; // Control ep0
            hid->device.endpoint_info[USB_CONTROL_EP].max_burst_size  = 0;
            hid->device.endpoint_info[1].max_packet_size              = 8; // In ep1
            hid->device.endpoint_info[1].max_burst_size               = 0;
        } else {
            hid->device.dev_descriptor                                = bx_mouse_dev_descriptor;
            hid->device.device_desc_size                              = sizeof(bx_mouse_dev_descriptor);
            hid->device.endpoint_info[USB_CONTROL_EP].max_packet_size = 8; // Control ep0
            hid->device.endpoint_info[USB_CONTROL_EP].max_burst_size  = 0;
            hid->device.endpoint_info[1].max_packet_size              = 8; // In ep1
            hid->device.endpoint_info[1].max_burst_size               = 0;
        }
        if (hid->device.type == USB_HID_TYPE_MOUSE) {
            if (hid->device.speed == USB_SPEED_HIGH) {
                hid->device.config_descriptor = bx_mouse_config_descriptor2;
                hid->device.config_desc_size  = sizeof(bx_mouse_config_descriptor2);
            } else {
                if (hid->s.model >= hid_mouse_3x3x8phy) {
                    hid->device.config_descriptor = bx_mouse_config_descriptor1;
                    hid->device.config_desc_size  = sizeof(bx_mouse_config_descriptor1);
                } else {
                    hid->device.config_descriptor = bx_mouse_config_descriptor0;
                    hid->device.config_desc_size  = sizeof(bx_mouse_config_descriptor0);
                }
            }
        } else {
            // Tablet
            if (hid->device.speed == USB_SPEED_HIGH) {
                hid->device.config_descriptor = bx_tablet_config_descriptor2;
                hid->device.config_desc_size  = sizeof(bx_tablet_config_descriptor2);
            } else {
                hid->device.config_descriptor = bx_tablet_config_descriptor;
                hid->device.config_desc_size  = sizeof(bx_tablet_config_descriptor);
            }
        }
        // initialize the correct hid descriptor
        if (hid->s.model == hid_mouse_2x2x8) {
            hid->s.bx_mouse_hid_report_descriptor     = bx_mouse_hid_report_descriptor_228;
            hid->s.bx_mouse_hid_report_descriptor_len = sizeof(bx_mouse_hid_report_descriptor_228);
        } else if (hid->s.model == hid_mouse_3x3x8) {
            hid->s.bx_mouse_hid_report_descriptor     = bx_mouse_hid_report_descriptor_338;
            hid->s.bx_mouse_hid_report_descriptor_len = sizeof(bx_mouse_hid_report_descriptor_338);
        } else if (hid->s.model == hid_mouse_3x3xDebug) {
            hid->s.bx_mouse_hid_report_descriptor     = bx_mouse_hid_report_descriptor_33debug;
            hid->s.bx_mouse_hid_report_descriptor_len = sizeof(bx_mouse_hid_report_descriptor_33debug);
        } else if (hid->s.model == hid_mouse_3x3x12) {
            hid->s.bx_mouse_hid_report_descriptor     = bx_mouse_hid_report_descriptor_3312;
            hid->s.bx_mouse_hid_report_descriptor_len = sizeof(bx_mouse_hid_report_descriptor_3312);
        } else if (hid->s.model == hid_mouse_3x3x16) {
            hid->s.bx_mouse_hid_report_descriptor     = bx_mouse_hid_report_descriptor_3316;
            hid->s.bx_mouse_hid_report_descriptor_len = sizeof(bx_mouse_hid_report_descriptor_3316);
        } else if (hid->s.model == hid_mouse_3x3x8phy) {
            hid->s.bx_mouse_hid_report_descriptor     = bx_mouse_hid_report_descriptor_338phy;
            hid->s.bx_mouse_hid_report_descriptor_len = sizeof(bx_mouse_hid_report_descriptor_338phy);
        }
        // update the hid descriptor length fields
        *(Bit16u *) &bx_mouse_config_descriptor0[BX_Mouse_Config_Descriptor0_pos] = *(Bit16u *) &bx_mouse_config_descriptor1[BX_Mouse_Config_Descriptor1_pos] = *(Bit16u *) &bx_mouse_config_descriptor2[BX_Mouse_Config_Descriptor2_pos] = *(Bit16u *) &bx_mouse_hid_descriptor0[BX_Mouse_Hid_Descriptor0] = *(Bit16u *) &bx_mouse_hid_descriptor1[BX_Mouse_Hid_Descriptor1] = hid->s.bx_mouse_hid_report_descriptor_len;
    }

    hid->device.connected     = 1;
    hid->device.alt_iface_max = 0;
    return 1;
}

void
usb_hid_handle_reset(usb_device_c *device)
{
    usb_device_hid *hid = (usb_device_hid *) device;
    memset((void *) &hid->s, 0, offsetof(struct HID_STATE, model));
    BX_DEBUG(("Reset"));

    // HID 1.11, section 7.2.6, page 54(64):
    //  "When initialized, all devices default to report protocol."
    hid->s.boot_protocol = PROTOCOL_REPORT;
    // next will be byte 2 in the 8 byte packet
    hid->s.kbd_packet_indx = 1;
}

void *
usb_hid_device_create(const device_t *info)
{
    usb_device_hid *hid = calloc(1, sizeof(usb_device_hid));

    usb_device_create(&hid->device);
    hid->device.type     = USB_HID_TYPE_MOUSE;
    hid->device.minspeed = USB_SPEED_LOW;
    hid->device.maxspeed = USB_SPEED_HIGH;
    hid->device.speed    = hid->device.minspeed;

    hid->device.vendor_desc  = "86Box";
    hid->device.product_desc = hid->device.devname;
    hid->device.serial_num   = "1";
    memset((void *) &hid->s, 0, sizeof(hid->s));

    // HID 1.11, section 7.2.6, page 54(64):
    //  "When initialized, all devices default to report protocol."
    hid->s.boot_protocol = PROTOCOL_REPORT;
    hid->s.report_id     = 0;
    if (hid->device.type == USB_HID_TYPE_MOUSE)
        hid->s.model = hid_mouse_3x3x8; // default model
    // next will be byte 2 in the 8 byte packet
    hid->s.kbd_packet_indx = 1;

    hid->device.handle_data    = usb_device_hid_handle_data;
    hid->device.handle_control = usb_device_hid_handle_control;
    hid->device.handle_reset   = usb_hid_handle_reset;
    hid->device.init           = usb_hid_device_init;

    timer_add(&hid->idle_timer, usb_device_hid_idle_timer, hid, 0);

    return hid;
}

void usb_uhci_close(void* priv)
{
    free(priv);
}

const device_t usb_mouse_device = {
    .name          = "USB Mouse",
    .internal_name = "usb_mouse",
    .flags         = DEVICE_USB,
    .local         = 0,
    .init          = usb_hid_device_create,
    .close         = usb_uhci_close,
    .reset         = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

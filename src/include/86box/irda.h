/* Function definition. */
typedef void irda_write(void* priv, uint8_t data);

typedef struct irda_device_t
{
    uint8_t pri;  /* Primary or secondary device. */
    uint8_t conn; /* Physical number of connection. */
    uint32_t src_addr; /* Logical address of source station. */
    uint32_t dst_addr; /* Logical address of source station. */

    irda_write *write; /* Write/receive function. */
    void* priv; /* Opaque pointer. */
} irda_device_t;

extern const device_t esi9680_device;

extern void irda_register_device(irda_device_t* irda_device);
extern void irda_broadcast_data(irda_device_t* broadcaster, uint8_t data);
extern void irda_reset(void);
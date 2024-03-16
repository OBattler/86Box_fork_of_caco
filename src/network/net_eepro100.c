#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <time.h>
#include <assert.h>
#include <string.h>

#include <86box/86box.h>
#include <86box/device.h>
#include <86box/pci.h>
#include <86box/mem.h>
#include <86box/thread.h>
#include <86box/timer.h>
#include <86box/network.h>
#include <86box/dma.h>
#include <86box/io.h>
#include <86box/plat_unused.h>
#include <86box/rom.h>
#include <86box/net_eeprom_nmc93cxx.h>
#include <86box/nvr.h>

#define CONFIG_PAD_RECEIVED_FRAMES

/* Set flags to 0 to disable debug output. */
#define INT     1       /* interrupt related actions */
#define MDI     1       /* mdi related actions */
#define OTHER   1
#define RXTX    1
#define EEPROM  1       /* eeprom related actions */

#define missing(text) pclog("net_eepro100.c: feature is missing in this emulation: " text "\n")
#define logout pclog

#define MAX_ETH_FRAME_SIZE 1514

/* This implementation supports several different devices which are declared here. */
#define i82550          0x82550
#define i82551          0x82551
#define i82557A         0x82557a
#define i82557B         0x82557b
#define i82557C         0x82557c
#define i82558A         0x82558a
#define i82558B         0x82558b
#define i82559A         0x82559a
#define i82559B         0x82559b
#define i82559C         0x82559c
#define i82559ER        0x82559e
#define i82562          0x82562
#define i82801          0x82801

#define EEPROM_SIZE     256

#define PCI_MEM_SIZE            (4 * 1024)
#define PCI_IO_SIZE             64
#define PCI_FLASH_SIZE          (128 * 1024)

#define BITS(n, m) (((0xffffffffU << (31 - n)) >> (31 - n + m)) << m)
#define BIT(x) (1 << x)

/* The SCB accepts the following controls for the Tx and Rx units: */
#define  CU_NOP         0x0000  /* No operation. */
#define  CU_START       0x0010  /* CU start. */
#define  CU_RESUME      0x0020  /* CU resume. */
#define  CU_STATSADDR   0x0040  /* Load dump counters address. */
#define  CU_SHOWSTATS   0x0050  /* Dump statistical counters. */
#define  CU_CMD_BASE    0x0060  /* Load CU base address. */
#define  CU_DUMPSTATS   0x0070  /* Dump and reset statistical counters. */
#define  CU_SRESUME     0x00a0  /* CU static resume. */

#define  RU_NOP         0x0000
#define  RX_START       0x0001
#define  RX_RESUME      0x0002
#define  RU_ABORT       0x0004
#define  RX_ADDR_LOAD   0x0006
#define  RX_RESUMENR    0x0007
#define INT_MASK        0x0100
#define DRVR_INT        0x0200  /* Driver generated interrupt. */

typedef struct {
    const char *name;
    const char *desc;
    uint16_t device_id;
    uint8_t revision;
    uint16_t subsystem_vendor_id;
    uint16_t subsystem_id;

    uint32_t device;
    uint8_t stats_size;
    bool has_extended_tcb_support;
    bool power_management;
} E100PCIDeviceInfo;

/* Offsets to the various registers.
   All accesses need not be longword aligned. */
typedef enum {
    SCBStatus = 0,              /* Status Word. */
    SCBAck = 1,
    SCBCmd = 2,                 /* Rx/Command Unit command and status. */
    SCBIntmask = 3,
    SCBPointer = 4,             /* General purpose pointer. */
    SCBPort = 8,                /* Misc. commands and operands.  */
    SCBflash = 12,              /* Flash memory control. */
    SCBeeprom = 14,             /* EEPROM control. */
    SCBCtrlMDI = 16,            /* MDI interface control. */
    SCBEarlyRx = 20,            /* Early receive byte count. */
    SCBFlow = 24,               /* Flow Control. */
    SCBpmdr = 27,               /* Power Management Driver. */
    SCBgctrl = 28,              /* General Control. */
    SCBgstat = 29,              /* General Status. */
} E100RegisterOffset;

/* A speedo3 transmit buffer descriptor with two buffers... */
typedef struct {
    uint16_t status;
    uint16_t command;
    uint32_t link;              /* void * */
    uint32_t tbd_array_addr;    /* transmit buffer descriptor array address. */
    uint16_t tcb_bytes;         /* transmit command block byte count (in lower 14 bits */
    uint8_t tx_threshold;       /* transmit threshold */
    uint8_t tbd_count;          /* TBD number */
#if 0
    /* This constitutes two "TBD" entries: hdr and data */
    uint32_t tx_buf_addr0;  /* void *, header of frame to be transmitted.  */
    int32_t  tx_buf_size0;  /* Length of Tx hdr. */
    uint32_t tx_buf_addr1;  /* void *, data to be transmitted.  */
    int32_t  tx_buf_size1;  /* Length of Tx data. */
#endif
} eepro100_tx_t;

/* Receive frame descriptor. */
typedef struct {
    int16_t status;
    uint16_t command;
    uint32_t link;              /* struct RxFD * */
    uint32_t rx_buf_addr;       /* void * */
    uint16_t count;
    uint16_t size;
    /* Ethernet frame data follows. */
} eepro100_rx_t;

typedef enum {
    COMMAND_EL = BIT(15),
    COMMAND_S = BIT(14),
    COMMAND_I = BIT(13),
    COMMAND_NC = BIT(4),
    COMMAND_SF = BIT(3),
    COMMAND_CMD = BITS(2, 0),
} scb_command_bit;

typedef enum {
    STATUS_C = BIT(15),
    STATUS_OK = BIT(13),
} scb_status_bit;

typedef struct {
    uint32_t tx_good_frames, tx_max_collisions, tx_late_collisions,
             tx_underruns, tx_lost_crs, tx_deferred, tx_single_collisions,
             tx_multiple_collisions, tx_total_collisions;
    uint32_t rx_good_frames, rx_crc_errors, rx_alignment_errors,
             rx_resource_errors, rx_overrun_errors, rx_cdt_errors,
             rx_short_frame_errors;
    uint32_t fc_xmt_pause, fc_rcv_pause, fc_rcv_unsupported;
    uint16_t xmt_tco_frames, rcv_tco_frames;
    /* TODO: i82559 has six reserved statistics but a total of 24 dwords. */
    uint32_t reserved[4];
} eepro100_stats_t;

typedef enum {
    cu_idle = 0,
    cu_suspended = 1,
    cu_active = 2,
    cu_lpq_active = 2,
    cu_hqp_active = 3
} cu_state_t;

typedef enum {
    ru_idle = 0,
    ru_suspended = 1,
    ru_no_resources = 2,
    ru_ready = 4
} ru_state_t;

#pragma pack(push, 2)
typedef struct {
    uint8_t mac_addr[6];
    uint16_t compat;
    uint16_t reserved;
    uint8_t connector;
    uint8_t controller;
    uint16_t phy_dev_record;
    uint16_t reserved_2;
    uint16_t pwa_number[2];
    uint16_t eeprom_id;
    uint16_t subsystem_id;
    uint16_t subsystem_vendor_id;
    uint16_t smbus_hb_packet;
    uint16_t reserved_3[34];
    uint16_t boot_agent_rom;
    uint16_t reserved_4[10];
    uint16_t boot_rom;
    uint16_t reserved_5[4];
    uint16_t alert_on_lan_pkt[187];
    uint16_t modem_vendor_id;
    uint16_t modem_device_id;
    uint16_t modem_interface;
    uint16_t modem_power_dissipation;
    uint16_t checksum;
} eepro100_eeprom_t;
#pragma pack(pop)

typedef struct {
    uint8_t dev;
    uint8_t irq_state;
    eepro100_eeprom_t* eeprom_data;
    uint8_t pci_conf[256];
    uint8_t macaddr[6];
    E100PCIDeviceInfo devinfo;
    /* Hash register (multicast mask array, multiple individual addresses). */
    uint8_t mult[8];
    mem_mapping_t mmio_bar;
    mem_mapping_t io_bar;
    mem_mapping_t flash_bar;
    rom_t expansion_rom;
    netcard_t *nic;
    uint8_t scb_stat;           /* SCB stat/ack byte */
    uint8_t int_stat;           /* PCI interrupt status */
    /* region must not be saved by nic_save. */
    uint16_t mdimem[32];
    nmc93cxx_eeprom_t *eeprom;
    uint32_t device;            /* device variant */
    /* (cu_base + cu_offset) address the next command block in the command block list. */
    uint32_t cu_base;           /* CU base address */
    uint32_t cu_offset;         /* CU address offset */
    /* (ru_base + ru_offset) address the RFD in the Receive Frame Area. */
    uint32_t ru_base;           /* RU base address */
    uint32_t ru_offset;         /* RU address offset */
    uint32_t statsaddr;         /* pointer to eepro100_stats_t */

    /* Temporary status information (no need to save these values),
     * used while processing CU commands. */
    eepro100_tx_t tx;           /* transmit buffer descriptor */
    uint32_t cb_address;        /* = cu_base + cu_offset */

    /* Statistical counters. Also used for wake-up packet (i82559). */
    eepro100_stats_t statistics;

    /* Data in mem is always in the byte order of the controller (le).
     * It must be dword aligned to allow direct access to 32 bit values. */
    uint8_t mem[PCI_MEM_SIZE] __attribute__((aligned(8)));

    /* Configuration bytes. */
    uint8_t configuration[22];

    /* Quasi static device properties (no need to save them). */
    uint16_t stats_size;
    bool has_extended_tcb_support;
} EEPRO100State;

/* Word indices in EEPROM. */
typedef enum {
    EEPROM_CNFG_MDIX  = 0x03,
    EEPROM_ID         = 0x05,
    EEPROM_PHY_ID     = 0x06,
    EEPROM_VENDOR_ID  = 0x0c,
    EEPROM_CONFIG_ASF = 0x0d,
    EEPROM_DEVICE_ID  = 0x23,
    EEPROM_SMBUS_ADDR = 0x90,
} EEPROMOffset;

/* Bit values for EEPROM ID word. */
typedef enum {
    EEPROM_ID_MDM = BIT(0),     /* Modem */
    EEPROM_ID_STB = BIT(1),     /* Standby Enable */
    EEPROM_ID_WMR = BIT(2),     /* ??? */
    EEPROM_ID_WOL = BIT(5),     /* Wake on LAN */
    EEPROM_ID_DPD = BIT(6),     /* Deep Power Down */
    EEPROM_ID_ALT = BIT(7),     /* */
    /* BITS(10, 8) device revision */
    EEPROM_ID_BD = BIT(11),     /* boot disable */
    EEPROM_ID_ID = BIT(13),     /* id bit */
    /* BITS(15, 14) signature */
    EEPROM_ID_VALID = BIT(14),  /* signature for valid eeprom */
} eeprom_id_bit;

/* Default values for MDI (PHY) registers */
static const uint16_t eepro100_mdi_default[] = {
    /* MDI Registers 0 - 6, 7 */
    0x3000, 0x780d, 0x02a8, 0x0154, 0x05e1, 0x0000, 0x0000, 0x0000,
    /* MDI Registers 8 - 15 */
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    /* MDI Registers 16 - 31 */
    0x0003, 0x0000, 0x0001, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
};

/* Readonly mask for MDI (PHY) registers */
static const uint16_t eepro100_mdi_mask[] = {
    0x0000, 0xffff, 0xffff, 0xffff, 0xc01f, 0xffff, 0xffff, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0fff, 0x0000, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
    0xffff, 0xffff, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
};

/* Read a 16 bit control/status (CSR) register. */
static uint16_t e100_read_reg2(EEPRO100State *s, E100RegisterOffset addr)
{
    assert(!((uintptr_t)&s->mem[addr] & 1));
    return s->mem[addr] | (s->mem[addr + 1]) << 8;
}

/* Read a 32 bit control/status (CSR) register. */
static uint32_t e100_read_reg4(EEPRO100State *s, E100RegisterOffset addr)
{
    assert(!((uintptr_t)&s->mem[addr] & 3));
    return s->mem[addr] | ((s->mem[addr + 1]) << 8) | ((s->mem[addr + 2]) << 16) | ((s->mem[addr + 3]) << 24);
}

/* Write a 16 bit control/status (CSR) register. */
static void e100_write_reg2(EEPRO100State *s, E100RegisterOffset addr,
                            uint16_t val)
{
    assert(!((uintptr_t)&s->mem[addr] & 1));
    s->mem[addr] = val & 0xFF;
    s->mem[addr + 1] = (val >> 8) & 0xFF;
}

/* Write a 32 bit control/status (CSR) register. */
static void e100_write_reg4(EEPRO100State *s, E100RegisterOffset addr,
                            uint32_t val)
{
    assert(!((uintptr_t)&s->mem[addr] & 3));

    s->mem[addr] = val & 0xFF;
    s->mem[addr + 1] = (val >> 8) & 0xFF;
    s->mem[addr + 2] = (val >> 16) & 0xFF;
    s->mem[addr + 3] = (val >> 24) & 0xFF;
}

enum scb_stat_ack {
    stat_ack_not_ours = 0x00,
    stat_ack_sw_gen = 0x04,
    stat_ack_rnr = 0x10,
    stat_ack_cu_idle = 0x20,
    stat_ack_frame_rx = 0x40,
    stat_ack_cu_cmd_done = 0x80,
    stat_ack_not_present = 0xFF,
    stat_ack_rx = (stat_ack_sw_gen | stat_ack_rnr | stat_ack_frame_rx),
    stat_ack_tx = (stat_ack_cu_idle | stat_ack_cu_cmd_done),
};

static void disable_interrupt(EEPRO100State * s)
{
    if (s->int_stat) {
        //pci_irq_deassert(&s->dev);
        pci_clear_irq(s->dev, PCI_INTA, &s->irq_state);
        s->int_stat = 0;
    }
}

static void enable_interrupt(EEPRO100State * s)
{
    if (!s->int_stat) {
        //pci_irq_assert(&s->dev);
        pci_set_irq(s->dev, PCI_INTA, &s->irq_state);
        s->int_stat = 1;
    }
}

static void eepro100_acknowledge(EEPRO100State * s)
{
    s->scb_stat &= ~s->mem[SCBAck];
    s->mem[SCBAck] = s->scb_stat;
    if (s->scb_stat == 0) {
        disable_interrupt(s);
    }
}

static void eepro100_interrupt(EEPRO100State * s, uint8_t status)
{
    uint8_t mask = ~s->mem[SCBIntmask];
    s->mem[SCBAck] |= status;
    status = s->scb_stat = s->mem[SCBAck];
    status &= (mask | 0x0f);
#if 0
    status &= (~s->mem[SCBIntmask] | 0x0xf);
#endif
    if (status && (mask & 0x01)) {
        /* SCB mask and SCB Bit M do not disable interrupt. */
        enable_interrupt(s);
    } else if (s->int_stat) {
        disable_interrupt(s);
    }
}

static void eepro100_cx_interrupt(EEPRO100State * s)
{
    /* CU completed action command. */
    /* Transmit not ok (82557 only, not in emulation). */
    eepro100_interrupt(s, 0x80);
}

static void eepro100_cna_interrupt(EEPRO100State * s)
{
    /* CU left the active state. */
    eepro100_interrupt(s, 0x20);
}

static void eepro100_fr_interrupt(EEPRO100State * s)
{
    /* RU received a complete frame. */
    eepro100_interrupt(s, 0x40);
}

static void eepro100_rnr_interrupt(EEPRO100State * s)
{
    /* RU is not ready. */
    eepro100_interrupt(s, 0x10);
}

static void eepro100_mdi_interrupt(EEPRO100State * s)
{
    /* MDI completed read or write cycle. */
    eepro100_interrupt(s, 0x08);
}

static void eepro100_swi_interrupt(EEPRO100State * s)
{
    /* Software has requested an interrupt. */
    eepro100_interrupt(s, 0x04);
}

#if 0
static void eepro100_fcp_interrupt(EEPRO100State * s)
{
    /* Flow control pause interrupt (82558 and later). */
    eepro100_interrupt(s, 0x01);
}
#endif
static void e100_pci_reset(void *p)
{
    EEPRO100State *s = (EEPRO100State*)p;
    E100PCIDeviceInfo *info = &s->devinfo;
    uint32_t device = s->device;
    uint8_t* pci_conf = s->pci_conf;

    pci_conf[0x06] = 0x80;
    pci_conf[0x07] = 0x2;
    pci_conf[0x0d] = 0x20;
    pci_conf[0x3e] = 0x08;
    pci_conf[0x3f] = 0x18;

    switch (device) {
    case i82550:
    case i82551:
    case i82557A:
    case i82557B:
    case i82557C:
    case i82558A:
    case i82558B:
    case i82559A:
    case i82559B:
    case i82559ER:
    case i82562:
    case i82801:
    case i82559C:
        break;
    default:
        pclog("Device %X is undefined!\n", device);
        break;
    }

    /* Standard TxCB. */
    s->configuration[6] |= BIT(4);

    /* Standard statistical counters. */
    s->configuration[6] |= BIT(5);

    if (s->stats_size == 80) {
        /* TODO: check TCO Statistical Counters bit. Documentation not clear. */
        if (s->configuration[6] & BIT(2)) {
            /* TCO statistical counters. */
            assert(s->configuration[6] & BIT(5));
        } else {
            if (s->configuration[6] & BIT(5)) {
                /* No extended statistical counters, i82557 compatible. */
                s->stats_size = 64;
            } else {
                /* i82558 compatible. */
                s->stats_size = 76;
            }
        }
    } else {
        if (s->configuration[6] & BIT(5)) {
            /* No extended statistical counters. */
            s->stats_size = 64;
        }
    }
    assert(s->stats_size > 0 && s->stats_size <= sizeof(s->statistics));

    if (info->power_management) {
        int cfg_offset = 0xdc;

        pci_conf[0x34] = cfg_offset;
        pci_conf[0x06] |= 0x10;

        pci_conf[cfg_offset + 2] = 0x21;
        pci_conf[cfg_offset + 3] = 0x7e;
    }

    if (device == i82557C || device == i82558B || device == i82559C) {
        if ((s->eeprom_data->eeprom_id & 0x3000) != 0x3000)
            info->subsystem_id = info->subsystem_vendor_id = 0x0;
        else {
            info->subsystem_id = s->eeprom_data->subsystem_id;
            info->subsystem_vendor_id = s->eeprom_data->subsystem_vendor_id;
        }
    }
}

static void nic_selective_reset(EEPRO100State * s)
{
    size_t i;
    uint16_t *eeprom_contents = nmc93cxx_eeprom_data(s->eeprom);
#if 0
    nmc93cxx_eeprom_reset(s->eeprom);
#endif
    //memcpy(eeprom_contents, s->macaddr, 6);
    eeprom_contents[EEPROM_ID] = EEPROM_ID_VALID;
    if (s->device == i82557B || s->device == i82557C)
        eeprom_contents[5] = 0x0100;
    eeprom_contents[EEPROM_PHY_ID] = 1;
    uint16_t sum = 0;
    for (i = 0; i < EEPROM_SIZE - 1; i++) {
        sum += eeprom_contents[i];
    }
    eeprom_contents[EEPROM_SIZE - 1] = 0xbaba - sum;

    memset(s->mem, 0, sizeof(s->mem));
    e100_write_reg4(s, SCBCtrlMDI, BIT(21));

    assert(sizeof(s->mdimem) == sizeof(eepro100_mdi_default));
    memcpy(&s->mdimem[0], &eepro100_mdi_default[0], sizeof(s->mdimem));
}

static void nic_reset(void *opaque)
{
    EEPRO100State *s = opaque;
    /* TODO: Clearing of hash register for selective reset, too? */
    memset(&s->mult[0], 0, sizeof(s->mult));
    memcpy(s->macaddr, s->eeprom_data->mac_addr, 6);
    nic_selective_reset(s);
}

/* Commands that can be put in a command list entry. */
enum commands {
    CmdNOp = 0,
    CmdIASetup = 1,
    CmdConfigure = 2,
    CmdMulticastList = 3,
    CmdTx = 4,
    CmdTDR = 5,                 /* load microcode */
    CmdDump = 6,
    CmdDiagnose = 7,

    /* And some extra flags: */
    CmdSuspend = 0x4000,        /* Suspend after completion. */
    CmdIntr = 0x2000,           /* Interrupt after completion. */
    CmdTxFlex = 0x0008,         /* Use "Flexible mode" for CmdTx command. */
};

static cu_state_t get_cu_state(EEPRO100State * s)
{
    return ((s->mem[SCBStatus] & BITS(7, 6)) >> 6);
}

static void set_cu_state(EEPRO100State * s, cu_state_t state)
{
    s->mem[SCBStatus] = (s->mem[SCBStatus] & ~BITS(7, 6)) + (state << 6);
}

static ru_state_t get_ru_state(EEPRO100State * s)
{
    return ((s->mem[SCBStatus] & BITS(5, 2)) >> 2);
}

static void set_ru_state(EEPRO100State * s, ru_state_t state)
{
    s->mem[SCBStatus] = (s->mem[SCBStatus] & ~BITS(5, 2)) + (state << 2);
}

static void dump_statistics(EEPRO100State * s)
{
    /* Dump statistical data. Most data is never changed by the emulation
     * and always 0, so we first just copy the whole block and then those
     * values which really matter.
     * Number of data should check configuration!!!
     */
    dma_bm_write(s->statsaddr, (uint8_t*)&s->statistics, s->stats_size, 1);
    dma_bm_write(s->statsaddr + 0, (uint8_t*)&s->statistics.tx_good_frames, 4, 4);
    dma_bm_write(s->statsaddr + 36, (uint8_t*)&s->statistics.rx_good_frames, 4, 4);
    dma_bm_write(s->statsaddr + 48, (uint8_t*)&s->statistics.rx_resource_errors, 4, 4);
    dma_bm_write(s->statsaddr + 60, (uint8_t*)&s->statistics.rx_short_frame_errors, 4, 4);
}


static void read_cb(EEPRO100State *s)
{
    dma_bm_read(s->cb_address, (uint8_t*)&s->tx, sizeof(s->tx), 4);
    s->tx.status = (s->tx.status);
    s->tx.command = (s->tx.command);
    s->tx.link = (s->tx.link);
    s->tx.tbd_array_addr = (s->tx.tbd_array_addr);
    s->tx.tcb_bytes = (s->tx.tcb_bytes);
}

static void tx_command(EEPRO100State *s)
{
    uint32_t tbd_array = s->tx.tbd_array_addr;
    uint16_t tcb_bytes = s->tx.tcb_bytes & 0x3fff;
    /* Sends larger than MAX_ETH_FRAME_SIZE are allowed, up to 2600 bytes. */
    uint8_t buf[2600];
    uint16_t size = 0;
    uint32_t tbd_address = s->cb_address + 0x10;

    if (tcb_bytes > 2600) {
        pclog("TCB byte count too large, using 2600\n");
        tcb_bytes = 2600;
    }
    if (!((tcb_bytes > 0) || (tbd_array != 0xffffffff))) {
        pclog
            ("illegal values of TBD array address and TCB byte count!\n");
    }
    assert(tcb_bytes <= sizeof(buf));
    while (size < tcb_bytes) {
        dma_bm_read(tbd_address, &buf[size], tcb_bytes, 1);
        size += tcb_bytes;
    }
    if (tbd_array == 0xffffffff) {
        /* Simplified mode. Was already handled by code above. */
    } else {
        /* Flexible mode. */
        uint8_t tbd_count = 0;
        uint32_t tx_buffer_address;
        uint16_t tx_buffer_size;
        uint16_t tx_buffer_el;

        if (s->has_extended_tcb_support && !(s->configuration[6] & BIT(4))) {
            /* Extended Flexible TCB. */
            for (; tbd_count < 2; tbd_count++) {
                dma_bm_read(tbd_address, (uint8_t*)&tx_buffer_address, 4, 4);
                dma_bm_read(tbd_address + 4, (uint8_t*)&tx_buffer_size, 2, 2);
                dma_bm_read(tbd_address + 6, (uint8_t*)&tx_buffer_el, 2, 2);
                tbd_address += 8;
                tx_buffer_size = MIN(tx_buffer_size, sizeof(buf) - size);
                dma_bm_read(tx_buffer_address, &buf[size], tx_buffer_size, 1);
                size += tx_buffer_size;
                if (tx_buffer_el & 1) {
                    break;
                }
            }
        }
        tbd_address = tbd_array;
        for (; tbd_count < s->tx.tbd_count; tbd_count++) {
            dma_bm_read(tbd_address, (uint8_t*)&tx_buffer_address, 4, 4);
            dma_bm_read(tbd_address + 4, (uint8_t*)&tx_buffer_size, 2, 2);
            dma_bm_read(tbd_address + 6, (uint8_t*)&tx_buffer_el, 2, 2);
            tbd_address += 8;
            tx_buffer_size = MIN(tx_buffer_size, sizeof(buf) - size);
            dma_bm_read(tx_buffer_address,
                         &buf[size], tx_buffer_size, 1);
            size += tx_buffer_size;
            if (tx_buffer_el & 1) {
                break;
            }
        }
    }
    //qemu_send_packet(qemu_get_queue(s->nic), buf, size);
    network_tx(s->nic, buf, size);
    s->statistics.tx_good_frames++;
    /* Transmit with bad status would raise an CX/TNO interrupt.
     * (82557 only). Emulation never has bad status. */
#if 0
    eepro100_cx_interrupt(s);
#endif
}

/* From FreeBSD */
/* XXX: optimize */
static uint32_t net_crc32(const uint8_t *p, int len)
{
    uint32_t crc;
    int carry, i, j;
    uint8_t b;

    crc = 0xffffffff;
    for (i = 0; i < len; i++) {
        b = *p++;
        for (j = 0; j < 8; j++) {
            carry = ((crc & 0x80000000L) ? 1 : 0) ^ (b & 0x01);
            crc <<= 1;
            b >>= 1;
            if (carry) {
                crc = ((crc ^ 0x04c11db6) | carry);
            }
        }
    }

    return crc;
}

static void set_multicast_list(EEPRO100State *s)
{
    uint16_t multicast_count = s->tx.tbd_array_addr & BITS(13, 0);
    uint16_t i;
    memset(&s->mult[0], 0, sizeof(s->mult));
    for (i = 0; i < multicast_count; i += 6) {
        uint8_t multicast_addr[6];
        dma_bm_read(s->cb_address + 10 + i, multicast_addr, 6, 1);
        unsigned mcast_idx = (net_crc32(multicast_addr, 6) &
                              BITS(7, 2)) >> 2;
        assert(mcast_idx < 64);
        s->mult[mcast_idx >> 3] |= (1 << (mcast_idx & 7));
    }
}

static void action_command(EEPRO100State *s)
{
    /* The loop below won't stop if it gets special handcrafted data.
       Therefore we limit the number of iterations. */
    unsigned max_loop_count = 16;

    for (;;) {
        bool bit_el;
        bool bit_s;
        bool bit_i;
        bool bit_nc;
        uint16_t ok_status = STATUS_OK;
        s->cb_address = s->cu_base + s->cu_offset;
        read_cb(s);
        bit_el = ((s->tx.command & COMMAND_EL) != 0);
        bit_s = ((s->tx.command & COMMAND_S) != 0);
        bit_i = ((s->tx.command & COMMAND_I) != 0);
        bit_nc = ((s->tx.command & COMMAND_NC) != 0);
#if 0
        bool bit_sf = ((s->tx.command & COMMAND_SF) != 0);
#endif

        if (max_loop_count-- == 0) {
            /* Prevent an endless loop. */
            logout("loop in %s:%u\n", __FILE__, __LINE__);
            break;
        }

        s->cu_offset = s->tx.link;
        switch (s->tx.command & COMMAND_CMD) {
        case CmdNOp:
            /* Do nothing. */
            break;
        case CmdIASetup:
            dma_bm_read(s->cb_address + 8, &s->macaddr[0], 6, 1);
            break;
        case CmdConfigure:
            dma_bm_read(s->cb_address + 8,
                         &s->configuration[0], sizeof(s->configuration), 1);
            break;
        case CmdMulticastList:
            set_multicast_list(s);
            break;
        case CmdTx:
            if (bit_nc) {
                missing("CmdTx: NC = 0");
                ok_status = 0;
                break;
            }
            tx_command(s);
            break;
        case CmdTDR:
            //TRACE(OTHER, logout("load microcode\n"));
            /* Starting with offset 8, the command contains
             * 64 dwords microcode which we just ignore here. */
            break;
        case CmdDiagnose:
            //TRACE(OTHER, logout("diagnose\n"));
            /* Make sure error flag is not set. */
            s->tx.status = 0;
            break;
        default:
            missing("undefined command");
            ok_status = 0;
            break;
        }
        /* Write new status. */
        mem_writew_phys(s->cb_address,
                       s->tx.status | ok_status | STATUS_C);
        if (bit_i) {
            /* CU completed action. */
            eepro100_cx_interrupt(s);
        }
        if (bit_el) {
            /* CU becomes idle. Terminate command loop. */
            set_cu_state(s, cu_idle);
            eepro100_cna_interrupt(s);
            break;
        } else if (bit_s) {
            /* CU becomes suspended. Terminate command loop. */
            set_cu_state(s, cu_suspended);
            eepro100_cna_interrupt(s);
            break;
        } else {
            /* More entries in list. */
        }
    }
    /* List is empty. Now CU is idle or suspended. */
}

static void eepro100_cu_command(EEPRO100State * s, uint8_t val)
{
    cu_state_t cu_state;
    switch (val) {
    case CU_NOP:
        /* No operation. */
        break;
    case CU_START:
        cu_state = get_cu_state(s);
        if (cu_state != cu_idle && cu_state != cu_suspended) {
            /* Intel documentation says that CU must be idle or suspended
             * for the CU start command. */
            pclog("unexpected CU state is %u\n", cu_state);
        }
        set_cu_state(s, cu_active);
        s->cu_offset = e100_read_reg4(s, SCBPointer);
        action_command(s);
        break;
    case CU_RESUME:
        if (get_cu_state(s) != cu_suspended) {
            pclog("bad CU resume from CU state %u\n", get_cu_state(s));
            /* Workaround for bad Linux eepro100 driver which resumes
             * from idle state. */
#if 0
            missing("cu resume");
#endif
            set_cu_state(s, cu_suspended);
        }
        if (get_cu_state(s) == cu_suspended) {
            set_cu_state(s, cu_active);
            action_command(s);
        }
        break;
    case CU_STATSADDR:
        /* Load dump counters address. */
        s->statsaddr = e100_read_reg4(s, SCBPointer);
        if (s->statsaddr & 3) {
            /* Memory must be Dword aligned. */
            pclog("unaligned dump counters address\n");
            /* Handling of misaligned addresses is undefined.
             * Here we align the address by ignoring the lower bits. */
            /* TODO: Test unaligned dump counter address on real hardware. */
            s->statsaddr &= ~3;
        }
        break;
    case CU_SHOWSTATS:
        /* Dump statistical counters. */
        dump_statistics(s);
        mem_writel_phys(s->statsaddr + s->stats_size, 0xa005);
        break;
    case CU_CMD_BASE:
        /* Load CU base. */
        s->cu_base = e100_read_reg4(s, SCBPointer);
        break;
    case CU_DUMPSTATS:
        /* Dump and reset statistical counters. */
        dump_statistics(s);
        mem_writel_phys(s->statsaddr + s->stats_size, 0xa007);
        memset(&s->statistics, 0, sizeof(s->statistics));
        break;
    case CU_SRESUME:
        /* CU static resume. */
        missing("CU static resume");
        break;
    default:
        missing("Undefined CU command");
    }
}

static void eepro100_ru_command(EEPRO100State * s, uint8_t val)
{
    switch (val) {
    case RU_NOP:
        /* No operation. */
        break;
    case RX_START:
        /* RU start. */
        if (get_ru_state(s) != ru_idle) {
            pclog("RU state is %u, should be %u\n", get_ru_state(s), ru_idle);
#if 0
            assert(!"wrong RU state");
#endif
        }
        set_ru_state(s, ru_ready);
        s->ru_offset = e100_read_reg4(s, SCBPointer);
        /* TODO: Maybe implement flush? Official programming manual doesn't tell us much. */
        break;
    case RX_RESUME:
        /* Restart RU. */
        if (get_ru_state(s) != ru_suspended) {
            pclog("RU state is %u, should be %u\n", get_ru_state(s),
                   ru_suspended);
#if 0
            assert(!"wrong RU state");
#endif
        }
        set_ru_state(s, ru_ready);
        break;
    case RU_ABORT:
        /* RU abort. */
        if (get_ru_state(s) == ru_ready) {
            eepro100_rnr_interrupt(s);
        }
        set_ru_state(s, ru_idle);
        break;
    case RX_ADDR_LOAD:
        /* Load RU base. */
        s->ru_base = e100_read_reg4(s, SCBPointer);
        break;
    default:
        pclog("val=0x%02x (undefined RU command)\n", val);
        missing("Undefined SU command");
    }
}

static void eepro100_write_command(EEPRO100State * s, uint8_t val)
{
    eepro100_ru_command(s, val & 0x0f);
    eepro100_cu_command(s, val & 0xf0);

    /* Clear command byte after command was accepted. */
    s->mem[SCBCmd] = 0;
}

/*****************************************************************************
 *
 * EEPROM emulation.
 *
 ****************************************************************************/

#define EEPROM_CS       0x02
#define EEPROM_SK       0x01
#define EEPROM_DI       0x04
#define EEPROM_DO       0x08

static uint16_t eepro100_read_eeprom(EEPRO100State * s)
{
    uint16_t val = e100_read_reg2(s, SCBeeprom);
    if (nmc93cxx_eeprom_read(s->eeprom)) {
        val |= EEPROM_DO;
    } else {
        val &= ~EEPROM_DO;
    }
    return val;
}

static void eepro100_write_eeprom(nmc93cxx_eeprom_t * eeprom, uint8_t val)
{
    /* mask unwritable bits */
#if 0
    val = SET_MASKED(val, 0x31, eeprom->value);
#endif

    int eecs = ((val & EEPROM_CS) != 0);
    int eesk = ((val & EEPROM_SK) != 0);
    int eedi = ((val & EEPROM_DI) != 0);
    nmc93cxx_eeprom_write(eeprom, eecs, eesk, eedi);
}

/*****************************************************************************
 *
 * MDI emulation.
 *
 ****************************************************************************/

static uint32_t eepro100_read_mdi(EEPRO100State * s)
{
    uint32_t val = e100_read_reg4(s, SCBCtrlMDI);

    /* Emulation takes no time to finish MDI transaction. */
    val |= BIT(28);
    return val;
}

static void eepro100_write_mdi(EEPRO100State *s)
{
    uint32_t val = e100_read_reg4(s, SCBCtrlMDI);
    uint8_t raiseint = (val & BIT(29)) >> 29;
    uint8_t opcode = (val & BITS(27, 26)) >> 26;
    uint8_t phy = (val & BITS(25, 21)) >> 21;
    uint8_t reg = (val & BITS(20, 16)) >> 16;
    uint16_t data = (val & BITS(15, 0));
    if (phy != 1) {
        /* Unsupported PHY address. */
#if 0
        logout("phy must be 1 but is %u\n", phy);
#endif
        data = 0;
    } else if (opcode != 1 && opcode != 2) {
        /* Unsupported opcode. */
        logout("opcode must be 1 or 2 but is %u\n", opcode);
        data = 0;
    } else if (reg > 6) {
        /* Unsupported register. */
        logout("register must be 0...6 but is %u\n", reg);
        data = 0;
    } else {
        if (opcode == 1) {
            /* MDI write */
            switch (reg) {
            case 0:            /* Control Register */
                if (data & 0x8000) {
                    /* Reset status and control registers to default. */
                    s->mdimem[0] = eepro100_mdi_default[0];
                    s->mdimem[1] = eepro100_mdi_default[1];
                    data = s->mdimem[reg];
                } else {
                    /* Restart Auto Configuration = Normal Operation */
                    data &= ~0x0200;
                }
                break;
            case 1:            /* Status Register */
                missing("not writable");
                break;
            case 2:            /* PHY Identification Register (Word 1) */
            case 3:            /* PHY Identification Register (Word 2) */
                missing("not implemented");
                break;
            case 4:            /* Auto-Negotiation Advertisement Register */
            case 5:            /* Auto-Negotiation Link Partner Ability Register */
                break;
            case 6:            /* Auto-Negotiation Expansion Register */
            default:
                missing("not implemented");
            }
            s->mdimem[reg] &= eepro100_mdi_mask[reg];
            s->mdimem[reg] |= data & ~eepro100_mdi_mask[reg];
        } else if (opcode == 2) {
            /* MDI read */
            switch (reg) {
            case 0:            /* Control Register */
                if (data & 0x8000) {
                    /* Reset status and control registers to default. */
                    s->mdimem[0] = eepro100_mdi_default[0];
                    s->mdimem[1] = eepro100_mdi_default[1];
                }
                break;
            case 1:            /* Status Register */
                s->mdimem[reg] |= 0x0020;
                break;
            case 2:            /* PHY Identification Register (Word 1) */
            case 3:            /* PHY Identification Register (Word 2) */
            case 4:            /* Auto-Negotiation Advertisement Register */
                break;
            case 5:            /* Auto-Negotiation Link Partner Ability Register */
                s->mdimem[reg] = 0x41fe;
                break;
            case 6:            /* Auto-Negotiation Expansion Register */
                s->mdimem[reg] = 0x0001;
                break;
            }
            data = s->mdimem[reg];
        }
        /* Emulation takes no time to finish MDI transaction.
         * Set MDI bit in SCB status register. */
        s->mem[SCBAck] |= 0x08;
        val |= BIT(28);
        if (raiseint) {
            eepro100_mdi_interrupt(s);
        }
    }
    val = (val & 0xffff0000) + data;
    e100_write_reg4(s, SCBCtrlMDI, val);
}

/*****************************************************************************
 *
 * Port emulation.
 *
 ****************************************************************************/

#define PORT_SOFTWARE_RESET     0
#define PORT_SELFTEST           1
#define PORT_SELECTIVE_RESET    2
#define PORT_DUMP               3
#define PORT_SELECTION_MASK     3

typedef struct {
    uint32_t st_sign;           /* Self Test Signature */
    uint32_t st_result;         /* Self Test Results */
} eepro100_selftest_t;

static uint32_t eepro100_read_port(EEPRO100State * s)
{
    return 0;
}

static void eepro100_write_port(EEPRO100State *s)
{
    uint32_t val = e100_read_reg4(s, SCBPort);
    uint32_t address = (val & ~PORT_SELECTION_MASK);
    uint8_t selection = (val & PORT_SELECTION_MASK);
    eepro100_selftest_t data;
    switch (selection) {
    case PORT_SOFTWARE_RESET:
        nic_reset(s);
        break;
    case PORT_SELFTEST:
        dma_bm_read(address, (uint8_t *) &data, sizeof(data), 1);
        data.st_sign = 0xffffffff;
        data.st_result = 0;
        dma_bm_write(address, (uint8_t *) &data, sizeof(data), 1);
        break;
    case PORT_SELECTIVE_RESET:
        nic_selective_reset(s);
        break;
    default:
        logout("val=0x%08x\n", val);
        missing("unknown port selection");
    }
}

/*****************************************************************************
 *
 * General hardware emulation.
 *
 ****************************************************************************/

static const char * const e100_reg[PCI_IO_SIZE / 4] = {
    "Command/Status",
    "General Pointer",
    "Port",
    "EEPROM/Flash Control",
    "MDI Control",
    "Receive DMA Byte Count",
    "Flow Control",
    "General Status/Control"
};

static char *regname(uint32_t addr)
{
    static char buf[32];
    if (addr < PCI_IO_SIZE) {
        const char *r = e100_reg[addr / 4];
        if (r != 0) {
            snprintf(buf, sizeof(buf), "%s+%u", r, addr % 4);
        } else {
            snprintf(buf, sizeof(buf), "0x%02x", addr);
        }
    } else {
        snprintf(buf, sizeof(buf), "??? 0x%08x", addr);
    }
    return buf;
}

static uint8_t eepro100_read1(uint32_t addr, void* p)
{
    uint8_t val = 0;
    EEPRO100State *s = (EEPRO100State*)p;
    addr &= 0xFFF;
    if (addr <= sizeof(s->mem) - sizeof(val)) {
        val = s->mem[addr];
    }

    switch (addr) {
    case SCBStatus:
    case SCBAck:
        break;
    case SCBCmd:
#if 0
        val = eepro100_read_command(s);
#endif
        break;
    case SCBIntmask:
        break;
    case SCBPort + 3:
        break;
    case SCBeeprom:
        val = eepro100_read_eeprom(s);
        break;
    case SCBCtrlMDI:
    case SCBCtrlMDI + 1:
    case SCBCtrlMDI + 2:
    case SCBCtrlMDI + 3:
        val = (uint8_t)(eepro100_read_mdi(s) >> (8 * (addr & 3)));
        break;
    case SCBpmdr:       /* Power Management Driver Register */
        val = 0;
        break;
    case SCBgctrl:      /* General Control Register */
        break;
    case SCBgstat:      /* General Status Register */
        /* TODO: Change this for our more complete link emulation. */
        /* 100 Mbps full duplex, valid link */
        val = 0x07;
        break;
    default:
        //logout("addr=%s val=0x%02x\n", regname(addr), val);
        //missing("unknown byte read");
        break;
    }
    return val;
}

static uint16_t eepro100_read2(uint32_t addr, void* p)
{
    uint16_t val = 0;
    EEPRO100State *s = (EEPRO100State*)p;
    addr &= 0xFFF;
    if (addr <= sizeof(s->mem) - sizeof(val)) {
        val = e100_read_reg2(s, addr);
    }

    switch (addr) {
    case SCBStatus:
    case SCBCmd:
        break;
    case SCBeeprom:
        val = eepro100_read_eeprom(s);
        break;
    case SCBCtrlMDI:
    case SCBCtrlMDI + 2:
        val = (uint16_t)(eepro100_read_mdi(s) >> (8 * (addr & 3)));
        break;
    default:
        logout("addr=%s val=0x%04x\n", regname(addr), val);
        missing("unknown word read");
    }
    return val;
}

static uint32_t eepro100_read4(uint32_t addr, void* p)
{
    uint32_t val = 0;
    EEPRO100State *s = (EEPRO100State*)p;
    addr &= 0xFFF;
    if (addr <= sizeof(s->mem) - sizeof(val)) {
        val = e100_read_reg4(s, addr);
    }

    switch (addr) {
    case SCBStatus:
        break;
    case SCBPointer:
        break;
    case SCBPort:
        val = eepro100_read_port(s);
        break;
    case SCBflash:
        val = eepro100_read_eeprom(s);
        break;
    case SCBCtrlMDI:
        val = eepro100_read_mdi(s);
        break;
    default:
        logout("addr=%s val=0x%08x\n", regname(addr), val);
        missing("unknown longword read");
    }
    return val;
}

static void eepro100_write1(uint32_t addr, uint8_t val, void* p)
{
    EEPRO100State *s = (EEPRO100State*)p;
    addr &= 0xFFF;
    /* SCBStatus is readonly. */
    if (addr > SCBStatus && addr <= sizeof(s->mem) - sizeof(val)) {
        s->mem[addr] = val;
    }

    switch (addr) {
    case SCBStatus:
        break;
    case SCBAck:
        eepro100_acknowledge(s);
        break;
    case SCBCmd:
        eepro100_write_command(s, val);
        break;
    case SCBIntmask:
        if (val & BIT(1)) {
            eepro100_swi_interrupt(s);
        }
        eepro100_interrupt(s, 0);
        break;
    case SCBPointer:
    case SCBPointer + 1:
    case SCBPointer + 2:
    case SCBPointer + 3:
        break;
    case SCBPort:
    case SCBPort + 1:
    case SCBPort + 2:
        break;
    case SCBPort + 3:
        eepro100_write_port(s);
        break;
    case SCBFlow:       /* does not exist on 82557 */
    case SCBFlow + 1:
    case SCBFlow + 2:
    case SCBpmdr:       /* does not exist on 82557 */
        break;
    case SCBeeprom:
        eepro100_write_eeprom(s->eeprom, val);
        break;
    case SCBCtrlMDI:
    case SCBCtrlMDI + 1:
    case SCBCtrlMDI + 2:
        break;
    case SCBCtrlMDI + 3:
        eepro100_write_mdi(s);
        break;
    default:
        //logout("addr=%s val=0x%02x\n", regname(addr), val);
        //missing("unknown byte write");
        break;
    }
}

static void eepro100_write2(uint32_t addr, uint16_t val, void* p)
{
    EEPRO100State *s = (EEPRO100State*)p;
    addr &= 0xFFF;
    /* SCBStatus is readonly. */
    if (addr > SCBStatus && addr <= sizeof(s->mem) - sizeof(val)) {
        e100_write_reg2(s, addr, val);
    }

    switch (addr) {
    case SCBStatus:
        s->mem[SCBAck] = (val >> 8);
        eepro100_acknowledge(s);
        break;
    case SCBCmd:
        eepro100_write_command(s, val);
        eepro100_write1(SCBIntmask, val >> 8, s);
        break;
    case SCBPointer:
    case SCBPointer + 2:
        break;
    case SCBPort:
        break;
    case SCBPort + 2:
        eepro100_write_port(s);
        break;
    case SCBeeprom:
        eepro100_write_eeprom(s->eeprom, val);
        break;
    case SCBCtrlMDI:
        break;
    case SCBCtrlMDI + 2:
        eepro100_write_mdi(s);
        break;
    default:
        logout("addr=%s val=0x%04x\n", regname(addr), val);
        missing("unknown word write");
    }
}

static void eepro100_write4(uint32_t addr, uint32_t val, void* p)
{
    EEPRO100State *s = (EEPRO100State*)p;
    addr &= 0xFFF;
    if (addr <= sizeof(s->mem) - sizeof(val)) {
        e100_write_reg4(s, addr, val);
    }

    switch (addr) {
    case SCBPointer:
        break;
    case SCBPort:
        eepro100_write_port(s);
        break;
    case SCBflash:
        val = val >> 16;
        eepro100_write_eeprom(s->eeprom, val);
        break;
    case SCBCtrlMDI:
        eepro100_write_mdi(s);
        break;
    default:
        logout("addr=%s val=0x%08x\n", regname(addr), val);
        missing("unknown longword write");
    }
}

static void eepro100_write4_io(uint16_t addr, uint32_t val, void* s)
{
    return eepro100_write4(addr & 63, val, s);
}

static uint32_t eepro100_read4_io(uint16_t addr, void* s)
{
    return eepro100_read4(addr & 63, s);
}

static void eepro100_write2_io(uint16_t addr, uint16_t val, void* s)
{
    return eepro100_write2(addr & 63, val, s);
}

static uint16_t eepro100_read2_io(uint16_t addr, void* s)
{
    return eepro100_read2(addr & 63, s);
}

static void eepro100_write1_io(uint16_t addr, uint8_t val, void* s)
{
    return eepro100_write1(addr & 63, val, s);
}

static uint8_t eepro100_read1_io(uint16_t addr, void* s)
{
    return eepro100_read1(addr & 63, s);
}

static int nic_receive(void *priv, uint8_t * buf, int size)
{
    /* TODO:
     * - Magic packets should set bit 30 in power management driver register.
     * - Interesting packets should set bit 29 in power management driver register.
     */
    EEPRO100State *s = (EEPRO100State*)priv;
    uint16_t rfd_status = 0xa000;
    eepro100_rx_t rx;
    uint16_t rfd_command, rfd_size;
#if defined(CONFIG_PAD_RECEIVED_FRAMES)
    uint8_t min_buf[60];
#endif
    static const uint8_t broadcast_macaddr[6] =
        { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

#if defined(CONFIG_PAD_RECEIVED_FRAMES)
    /* Pad to minimum Ethernet frame length */
    if (size < sizeof(min_buf)) {
        memcpy(min_buf, buf, size);
        memset(&min_buf[size], 0, sizeof(min_buf) - size);
        buf = min_buf;
        size = sizeof(min_buf);
    }
#endif

    if (s->configuration[8] & 0x80) {
        /* CSMA is disabled. */
        logout("%p received while CSMA is disabled\n", s);
        return 0;
#if !defined(CONFIG_PAD_RECEIVED_FRAMES)
    } else if (size < 64 && (s->configuration[7] & BIT(0))) {
        /* Short frame and configuration byte 7/0 (discard short receive) set:
         * Short frame is discarded */
        logout("%p received short frame (%zu byte)\n", s, size);
        s->statistics.rx_short_frame_errors++;
        return 0;
#endif
    } else if ((size > MAX_ETH_FRAME_SIZE + 4) && !(s->configuration[18] & BIT(3))) {
        /* Long frame and configuration byte 18/3 (long receive ok) not set:
         * Long frames are discarded. */
        logout("%p received long frame (%d byte), ignored\n", s, size);
        return 0;
    } else if (memcmp(buf, s->macaddr, 6) == 0) {       /* !!! */
        /* Frame matches individual address. */
        /* TODO: check configuration byte 15/4 (ignore U/L). */
    } else if (memcmp(buf, broadcast_macaddr, 6) == 0) {
        /* Broadcast frame. */
        rfd_status |= 0x0002;
    } else if (buf[0] & 0x01) {
        /* Multicast frame. */
        if (s->configuration[21] & BIT(3)) {
          /* Multicast all bit is set, receive all multicast frames. */
        } else {
          unsigned mcast_idx = (net_crc32(buf, 6) & BITS(7, 2)) >> 2;
          assert(mcast_idx < 64);
          if (s->mult[mcast_idx >> 3] & (1 << (mcast_idx & 7))) {
            /* Multicast frame is allowed in hash table. */
          } else if (s->configuration[15] & BIT(0)) {
              /* Promiscuous: receive all. */
              rfd_status |= 0x0004;
          } else {
              return 0;
          }
        }
        /* TODO: Next not for promiscuous mode? */
        rfd_status |= 0x0002;
    } else if (s->configuration[15] & BIT(0)) {
        /* Promiscuous: receive all. */
        rfd_status |= 0x0004;
    } else if (s->configuration[20] & BIT(6)) {
        /* Multiple IA bit set. */
        unsigned mcast_idx = net_crc32(buf, 6) >> 26;
        assert(mcast_idx < 64);
        if (s->mult[mcast_idx >> 3] & (1 << (mcast_idx & 7))) {
        } else {
            return 0;
        }
    } else {
        return 1;
    }

    if (get_ru_state(s) != ru_ready) {
        /* No resources available. */
        logout("no resources, state=%u\n", get_ru_state(s));
        /* TODO: RNR interrupt only at first failed frame? */
        eepro100_rnr_interrupt(s);
        s->statistics.rx_resource_errors++;
#if 0
        assert(!"no resources");
#endif
        return 0;
    }
    /* !!! */
    dma_bm_read(s->ru_base + s->ru_offset,
                 (uint8_t*)&rx, sizeof(eepro100_rx_t), 1);
    rfd_command = (rx.command);
    rfd_size = (rx.size);

    if (size > rfd_size) {
        logout("Receive buffer (%hd bytes) too small for data "
            "(%d bytes); data truncated\n", rfd_size, size);
        size = rfd_size;
    }
#if !defined(CONFIG_PAD_RECEIVED_FRAMES)
    if (size < 64) {
        rfd_status |= 0x0080;
    }
#endif
    mem_writew_phys(s->ru_base + s->ru_offset +
                __builtin_offsetof(eepro100_rx_t, status), rfd_status);
    mem_writew_phys(s->ru_base + s->ru_offset +
                __builtin_offsetof(eepro100_rx_t, count), size);
    /* Early receive interrupt not supported. */
#if 0
    eepro100_er_interrupt(s);
#endif
    /* Receive CRC Transfer not supported. */
    if (s->configuration[18] & BIT(2)) {
        missing("Receive CRC Transfer");
        return 0;
    }
    /* TODO: check stripping enable bit. */
#if 0
    assert(!(s->configuration[17] & BIT(0)));
#endif
    dma_bm_write(s->ru_base + s->ru_offset +
                  sizeof(eepro100_rx_t), buf, size, 1);
    s->statistics.rx_good_frames++;
    eepro100_fr_interrupt(s);
    s->ru_offset = (rx.link);
    if (rfd_command & COMMAND_EL) {
        /* EL bit is set, so this was the last frame. */
        logout("receive: Running out of frames\n");
        set_ru_state(s, ru_no_resources);
        eepro100_rnr_interrupt(s);
    }
    if (rfd_command & COMMAND_S) {
        /* S bit is set. */
        set_ru_state(s, ru_suspended);
    }
    return 1;
}

/* Intel (0x8086) */
#define PCI_DEVICE_ID_INTEL_82551IT      0x1209
#define PCI_DEVICE_ID_INTEL_82557        0x1229
#define PCI_DEVICE_ID_INTEL_82801IR      0x2922


static E100PCIDeviceInfo e100_devices[] = {
    {
        .name = "i82550",
        .desc = "Intel i82550 Ethernet",
        .device = i82550,
        /* TODO: check device id. */
        .device_id = PCI_DEVICE_ID_INTEL_82551IT,
        /* Revision ID: 0x0c, 0x0d, 0x0e. */
        .revision = 0x0e,
        /* TODO: check size of statistical counters. */
        .stats_size = 80,
        /* TODO: check extended tcb support. */
        .has_extended_tcb_support = true,
        .power_management = true,
    },{
        .name = "i82551",
        .desc = "Intel i82551 Ethernet",
        .device = i82551,
        .device_id = PCI_DEVICE_ID_INTEL_82551IT,
        /* Revision ID: 0x0f, 0x10. */
        .revision = 0x0f,
        /* TODO: check size of statistical counters. */
        .stats_size = 80,
        .has_extended_tcb_support = true,
        .power_management = true,
    },{
        .name = "i82557a",
        .desc = "Intel i82557A Ethernet",
        .device = i82557A,
        .device_id = PCI_DEVICE_ID_INTEL_82557,
        .revision = 0x01,
        .power_management = false,
    },{
        .name = "i82557b",
        .desc = "Intel i82557B Ethernet",
        .device = i82557B,
        .device_id = PCI_DEVICE_ID_INTEL_82557,
        .revision = 0x02,
        .power_management = false,
    },{
        .name = "i82557c",
        .desc = "Intel i82557C Ethernet",
        .device = i82557C,
        .device_id = PCI_DEVICE_ID_INTEL_82557,
        .revision = 0x03,
        .power_management = false,
    },{
        .name = "i82558a",
        .desc = "Intel i82558A Ethernet",
        .device = i82558A,
        .device_id = PCI_DEVICE_ID_INTEL_82557,
        .revision = 0x04,
        .stats_size = 76,
        .has_extended_tcb_support = true,
        .power_management = true,
    },{
        .name = "i82558b",
        .desc = "Intel i82558B Ethernet",
        .device = i82558B,
        .device_id = PCI_DEVICE_ID_INTEL_82557,
        .revision = 0x05,
        .stats_size = 76,
        .has_extended_tcb_support = true,
        .power_management = true,
    },{
        .name = "i82559a",
        .desc = "Intel i82559A Ethernet",
        .device = i82559A,
        .device_id = PCI_DEVICE_ID_INTEL_82557,
        .revision = 0x06,
        .stats_size = 80,
        .has_extended_tcb_support = true,
        .power_management = true,
    },{
        .name = "i82559b",
        .desc = "Intel i82559B Ethernet",
        .device = i82559B,
        .device_id = PCI_DEVICE_ID_INTEL_82557,
        .revision = 0x07,
        .stats_size = 80,
        .has_extended_tcb_support = true,
        .power_management = true,
    },{
        .name = "i82559c",
        .desc = "Intel i82559C Ethernet",
        .device = i82559C,
        .device_id = PCI_DEVICE_ID_INTEL_82557,
#if 0
        .revision = 0x08,
#endif
        /* TODO: Windows wants revision id 0x0c. */
        .revision = 0x0c,
#if EEPROM_SIZE > 0
        .subsystem_vendor_id = 0x8086,
        .subsystem_id = 0x0040,
#endif
        .stats_size = 80,
        .has_extended_tcb_support = true,
        .power_management = true,
    },{
        .name = "i82559er",
        .desc = "Intel i82559ER Ethernet",
        .device = i82559ER,
        .device_id = PCI_DEVICE_ID_INTEL_82551IT,
        .revision = 0x09,
        .stats_size = 80,
        .has_extended_tcb_support = true,
        .power_management = true,
    },{
        .name = "i82562",
        .desc = "Intel i82562 Ethernet",
        .device = i82562,
        /* TODO: check device id. */
        .device_id = PCI_DEVICE_ID_INTEL_82551IT,
        /* TODO: wrong revision id. */
        .revision = 0x0e,
        .stats_size = 80,
        .has_extended_tcb_support = true,
        .power_management = true,
    },{
        /* Toshiba Tecra 8200. */
        .name = "i82801",
        .desc = "Intel i82801 Ethernet",
        .device = i82801,
        .device_id = 0x2449,
        .revision = 0x03,
        .stats_size = 80,
        .has_extended_tcb_support = true,
        .power_management = true,
    }
};

static eepro100_eeprom_t eepro100_plus_default = 
{
    .mac_addr = { 0x00, 0xAA, 0x00, 0x24, 0x57, 0x65 },
    .compat = (0x2 | 0x1) | ((0x2 | 0x1) << 8), /* Client adapter. */
    .connector = 0x01,
    .controller = 0x02, /* 82559 */
    .phy_dev_record = 0x4701,
    .pwa_number = { 0x2535, 0x0309 },
    .eeprom_id = 0x40C0,
    .subsystem_id = 0x000B,
    .subsystem_vendor_id = 0x8086,
    .boot_agent_rom = 0x4 /* Enable BBS Boot */
};

static uint8_t
eepro100_pci_read(UNUSED(int func), int addr, void *priv)
{
    const EEPRO100State *dev = (EEPRO100State *) priv;
    switch (addr)
    {
        default:
            return dev->pci_conf[addr & 0xFF];
        case 0x00:
            return ((dev->eeprom_data->eeprom_id & 0x3000) == 0x3000) ? (dev->devinfo.subsystem_vendor_id & 0xFF) : 0x86;
        case 0x01:
            return ((dev->eeprom_data->eeprom_id & 0x3000) == 0x3000) ? (dev->devinfo.subsystem_vendor_id >> 8) : 0x80;
        case 0x02:
            return dev->devinfo.device_id & 0xFF;
        case 0x03:
            return dev->devinfo.device_id >> 8;
        case 0x04:
            return dev->pci_conf[addr & 0xFF] & 0x57;
        case 0x05:
            return dev->pci_conf[addr & 0xFF] & 0x1;
        case 0x06:
            return 0x9;
        case 0x07:
            return 0x2;
        case 0x08:
            return dev->devinfo.revision;
        case 0x09:
        case 0x0A:
            return 0x0;
        case 0x0B:
            return 0x02;
        case 0x0E:
        case 0x0F:
            return 0x00;
        case 0x10:
            return 0x08;
        case 0x11:
        case 0x12:
        case 0x13:
            return dev->mmio_bar.base >> (8 * (addr & 3));
        case 0x2C:
            return dev->devinfo.subsystem_id & 0xFF;
        case 0x2D:
            return dev->devinfo.subsystem_id >> 8;
        case 0x2E:
            return dev->devinfo.subsystem_vendor_id & 0xFF;
        case 0x2F:
            return dev->devinfo.subsystem_vendor_id >> 8;
        case 0x3D:
            return PCI_INTA;
        case 0xDE:
            return dev->pci_conf[addr & 0xFF] & 0x3;
        case 0xDF:            
            return dev->pci_conf[addr & 0xFF] & 0x1f;
    }
}

static void
eepro100_pci_write(UNUSED(int func), int addr, uint8_t val, void *priv)
{
    EEPRO100State *dev = (EEPRO100State *) priv;
    switch (addr)
    {
        case 0x04:
            dev->pci_conf[addr & 0xFF] = val & 0x57;
            break;
        case 0x05:
            dev->pci_conf[addr & 0xFF] = val & 0x1;
            break;
        case 0x3c:
            dev->pci_conf[addr & 0xFF] = val;
            break;
        case 0x11:
            dev->pci_conf[addr & 0xFF] = val & ~0xf;
            mem_mapping_set_addr(&dev->mmio_bar, (dev->pci_conf[0x11] << 8) | (dev->pci_conf[0x12] << 16) | (dev->pci_conf[0x13] << 24), PCI_MEM_SIZE);
            break;
        case 0x12:
        case 0x13:
            dev->pci_conf[addr & 0xFF] = val;
            mem_mapping_set_addr(&dev->mmio_bar, (dev->pci_conf[0x11] << 8) | (dev->pci_conf[0x12] << 16) | (dev->pci_conf[0x13] << 24), PCI_MEM_SIZE);
            break;
        case 0x14:
            val &= 0xC0;
            val |= 1;
        case 0x15:
            io_removehandler((dev->pci_conf[0x14] & 0xC0) | (dev->pci_conf[0x15] << 8), PCI_IO_SIZE, eepro100_read1_io, eepro100_read2_io, eepro100_read4_io, eepro100_write1_io, eepro100_write2_io, eepro100_write4_io, priv);
            dev->pci_conf[addr & 0xFF] = val;
            pclog("IOADDR = 0x%X\n", (dev->pci_conf[0x14] & 0xC0) | (dev->pci_conf[0x15] << 8));
            io_sethandler((dev->pci_conf[0x14] & 0xC0) | (dev->pci_conf[0x15] << 8), PCI_IO_SIZE, eepro100_read1_io, eepro100_read2_io, eepro100_read4_io, eepro100_write1_io, eepro100_write2_io, eepro100_write4_io, priv);
            break;
        case 0x1A:
        case 0x1B:
            {
                uint32_t mem_addr_base = 0;
                dev->pci_conf[addr & 0xFF] = val;
                if (addr == 0x1B)
                    dev->pci_conf[addr & 0xFF] &= 0xF0;
                mem_mapping_disable(&dev->flash_bar);
                mem_addr_base = (dev->pci_conf[0x1B] << 24) | (dev->pci_conf[0x1A] << 16);
                if (mem_addr_base)
                    mem_mapping_set_addr(&dev->flash_bar, mem_addr_base, 0x100000);
                break;
            }
        case 0x30:
            val &= 1;
        case 0x31:
        case 0x32:
        case 0x33:
            if (dev->eeprom_data->eeprom_id & (1 << 11))
                return;
            dev->pci_conf[addr & 0xFF] = val;
            if (addr == 0x31)
                dev->pci_conf[addr & 0xFF] = 0;
            if (addr == 0x32)
                dev->pci_conf[addr & 0xFF] &= 0xF0;
            mem_mapping_disable(&dev->expansion_rom.mapping);
            if (dev->pci_conf[0x30] & 0x1)
                mem_mapping_set_addr(&dev->expansion_rom.mapping, (dev->pci_conf[0x33] << 24) | (dev->pci_conf[0x32] << 16), 0x100000);
            break;
    }
}

static E100PCIDeviceInfo eepro100_plus_info = {
    .name = "i82559b",
    .desc = "Intel i82559B Ethernet",
    .device = i82559B,
    .device_id = PCI_DEVICE_ID_INTEL_82557,
    .revision = 0x07,
    .stats_size = 80,
    .has_extended_tcb_support = true,
    .power_management = true,
};

static void
rom_write(uint32_t addr, uint8_t val, void *priv)
{
    const rom_t *rom = (rom_t *) priv;

#ifdef ROM_TRACE
    if (rom->mapping.base == ROM_TRACE)
        rom_log("ROM: read byte from BIOS at %06lX\n", addr);
#endif

    if (addr < rom->mapping.base)
        return;
    if (addr >= (rom->mapping.base + rom->sz))
        return;
    rom->rom[(addr - rom->mapping.base) & rom->mask] = val;
}

static void
rom_writew(uint32_t addr, uint16_t val, void *priv)
{
    rom_t *rom = (rom_t *) priv;

#ifdef ROM_TRACE
    if (rom->mapping.base == ROM_TRACE)
        rom_log("ROM: read word from BIOS at %06lX\n", addr);
#endif

    if (addr < (rom->mapping.base - 1))
        return;
    if (addr >= (rom->mapping.base + rom->sz))
        return;
    *(uint16_t *) &rom->rom[(addr - rom->mapping.base) & rom->mask] = val;
}

static void
rom_writel(uint32_t addr, uint32_t val, void *priv)
{
    rom_t *rom = (rom_t *) priv;

#ifdef ROM_TRACE
    if (rom->mapping.base == ROM_TRACE)
        rom_log("ROM: read long from BIOS at %06lX\n", addr);
#endif

    if (addr < (rom->mapping.base - 3))
        return;
    if (addr >= (rom->mapping.base + rom->sz))
        return;
   *(uint32_t *) &rom->rom[(addr - rom->mapping.base) & rom->mask] = val;
}

static void*
eepro100_init(const device_t* info)
{
    nmc93cxx_eeprom_params_t eeprom_params = { EEPROM_SIZE, NULL, NULL };
    EEPRO100State *s = calloc(1, sizeof(EEPRO100State));
    FILE* eeprom_file = NULL;

    s->device = ((E100PCIDeviceInfo*)info->local)->device;
    
    rom_init(&s->expansion_rom, "roms/network/eepro100/ipxe.rom", 0, 0, 0x1FFFF, 0, MEM_MAPPING_EXTERNAL);

    eeprom_params.filename = "eepro100_eeprom.nvr";
    eeprom_params.default_content = (uint16_t*)&eepro100_plus_default;
    s->eeprom = device_add_parameters(&nmc93cxx_device, &eeprom_params);
    if (!s->eeprom) {
        free(s);
        return NULL;
    }

    s->eeprom_data = (eepro100_eeprom_t*)nmc93cxx_eeprom_data(s->eeprom);
    s->nic = network_attach(s, s->eeprom_data->mac_addr, nic_receive, NULL);
    s->devinfo = eepro100_plus_info;
    e100_pci_reset(s);

    nic_reset(s);

    mem_mapping_add(&s->mmio_bar, 0, 0, eepro100_read1, eepro100_read2, eepro100_read4, eepro100_write1, eepro100_write2, eepro100_write4, NULL, MEM_MAPPING_EXTERNAL, s);
    /* Need to *absolutely* fix this... */
    mem_mapping_add(&s->flash_bar, 0, 0, rom_read, rom_readw, rom_readl, rom_write, rom_writew, rom_writel, NULL, MEM_MAPPING_EXTERNAL, &s->expansion_rom);

    pci_add_card(PCI_CARD_NETWORK, eepro100_pci_read, eepro100_pci_write, s, &s->dev);

    return s;
}

static void
eepro100_close(void *priv)
{
    EEPRO100State *s = (EEPRO100State*)priv;
    FILE* eeprom_file = nvr_fopen("eepro100_eeprom.nvr", "wb");
    if (eeprom_file){
        fwrite(nmc93cxx_eeprom_data(s->eeprom), sizeof(uint16_t), EEPROM_SIZE, eeprom_file);
        fclose(eeprom_file);
    }
    free(priv);
}

static int
eepro100_available(void)
{
    return rom_present("roms/network/eepro100/ipxe.rom");
}

const device_t eepro100_device = {
    .name          = "Intel EtherExpress PRO/100+",
    .internal_name = "eepro100",
    .flags         = DEVICE_PCI,
    .local         = (uintptr_t)&eepro100_plus_info,
    .init          = eepro100_init,
    .close         = eepro100_close,
    .reset         = nic_reset,
    { .available = eepro100_available },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

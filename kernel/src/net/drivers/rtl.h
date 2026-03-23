#pragma once

/*
 * rtl8139.h — Unified Realtek Fast/Gigabit Ethernet driver
 *
 * Supports the full RTL8139/8169 family via a chip table, similar to
 * Linux's r8169 driver.  At probe time we walk the table and match on
 * PCI vendor:device ID; the matched entry carries per-chip capability
 * flags so the common init/TX/RX paths can handle every variant.
 *
 * Architecture split
 * ------------------
 *   RTL8139 family  – I/O-port access, 4 flat TX slots, flat RX ring.
 *   RTL8169 family  – MMIO access, descriptor-ring TX/RX (64/256 descs).
 *
 * Both families share the same RTLDev handle and the same public API:
 *   rtl_init()  rtl_send()  rtl_recv()
 */

#include "common.h"

/* -----------------------------------------------------------------------
 * Chip identity table (mirrors Linux r8169_pci_tbl / rtl_chip_infos)
 * ----------------------------------------------------------------------- */

typedef enum {
    /* RTL8139 family – legacy Fast Ethernet (10/100), I/O port */
    RTL_8139        = 0,
    RTL_8139A,
    RTL_8139AG,
    RTL_8139B,
    RTL_8130,
    RTL_8139C,
    RTL_8100,
    RTL_8100B_8139D,
    RTL_8139CP,
    RTL_8101L,

    /* RTL8169 family – Gigabit Ethernet, MMIO, descriptor rings */
    RTL_8169        = 32,
    RTL_8169S,
    RTL_8110S,
    RTL_8169SB,
    RTL_8169SC_8110SC,
    RTL_8168B,
    RTL_8168BB,
    RTL_8168C,
    RTL_8168CP,
    RTL_8168D,
    RTL_8168DP,
    RTL_8168E,
    RTL_8168EP,
    RTL_8168F,
    RTL_8168G,
    RTL_8168GU,
    RTL_8168H,
    RTL_8111B,
    RTL_8111C,
    RTL_8111DP,
    RTL_8111E,
    RTL_8111EP,
    RTL_8111F,
    RTL_8111G,
    RTL_8111GU,
    RTL_8111H,
    RTL_8100E,
    RTL_8101E,
    RTL_8102E,
    RTL_8103E,
    RTL_8401,
    RTL_8402,
} RTLChipId;

/* Capability flags (per-chip) */
#define RTL_CAP_MMIO        (1 << 0)  /* MMIO (vs I/O port)              */
#define RTL_CAP_DESC_RING   (1 << 1)  /* descriptor-ring TX/RX           */
#define RTL_CAP_64BIT_DMA   (1 << 2)  /* chip can address > 4 GiB        */
#define RTL_CAP_JUMBO       (1 << 3)  /* supports jumbo frames           */
#define RTL_CAP_CSUM        (1 << 4)  /* hardware checksum offload       */
#define RTL_CAP_MSI         (1 << 5)  /* MSI interrupt capable           */
#define RTL_CAP_ASPM        (1 << 6)  /* PCIe ASPM power management      */

typedef struct {
    uint16_t    vendor_id;
    uint16_t    device_id;
    uint16_t    subvendor;   /* 0xFFFF = wildcard */
    uint16_t    subdevice;   /* 0xFFFF = wildcard */
    RTLChipId   chip_id;
    uint32_t    caps;
    const char *name;
} RTLChipInfo;

/* -----------------------------------------------------------------------
 * Register map – RTL8139 (I/O port, 8-bit base)
 * ----------------------------------------------------------------------- */

/* MAC address */
#define RTL139_IDR0         0x00
/* Multicast filter */
#define RTL139_MAR0         0x08
#define RTL139_MAR4         0x0C
/* TX: 4 descriptor slots */
#define RTL139_TSD0         0x10
#define RTL139_TSD1         0x14
#define RTL139_TSD2         0x18
#define RTL139_TSD3         0x1C
#define RTL139_TSAD0        0x20
#define RTL139_TSAD1        0x24
#define RTL139_TSAD2        0x28
#define RTL139_TSAD3        0x2C
/* RX ring */
#define RTL139_RBSTART      0x30
#define RTL139_CR           0x37
#define RTL139_CAPR         0x38
#define RTL139_CBR          0x3A
#define RTL139_IMR          0x3C
#define RTL139_ISR          0x3E
#define RTL139_TCR          0x40
#define RTL139_RCR          0x44
#define RTL139_CFG9346      0x50
#define RTL139_CONFIG1      0x52
#define RTL139_MSR          0x58

/* CR bits */
#define RTL139_CR_RST       (1 << 4)
#define RTL139_CR_RE        (1 << 3)
#define RTL139_CR_TE        (1 << 2)
#define RTL139_CR_BUFE      (1 << 0)

/* TSD bits */
#define RTL139_TSD_OWN      (1 << 13)
#define RTL139_TSD_TOK      (1 << 15)
#define RTL139_TSD_SIZE     0x1FFF

/* ISR bits */
#define RTL139_ISR_ROK      (1 << 0)
#define RTL139_ISR_RXOVW    (1 << 4)
#define RTL139_ISR_FOVW     (1 << 6)

/* RX header flags */
#define RTL139_RX_ROK       (1 << 0)
#define RTL139_RX_ERR_MASK  (0x001E)  /* CRC|FAE|RUNT|LONG */

/* TCR / RCR */
#define RTL139_TCR_IFG_STD  (3u << 24)
#define RTL139_TCR_MXDMA    (7u << 8)
#define RTL139_RCR_APM      (1u << 1)
#define RTL139_RCR_AB       (1u << 3)
#define RTL139_RCR_AM       (1u << 2)
#define RTL139_RCR_WRAP     (1u << 7)
#define RTL139_RCR_MXDMA    (7u << 8)
#define RTL139_RCR_RBLEN64K (3u << 11)
#define RTL139_RCR_RXFTH    (7u << 13)

/* Sizes */
#define RTL139_TX_SLOTS     4
#define RTL139_TX_BUF_SZ    2048
#define RTL139_RX_BUF_SZ    (64*1024 + 16 + 1500)

/* -----------------------------------------------------------------------
 * Register map – RTL8169 (MMIO, 8-bit offsets, 32-bit-wide registers)
 * ----------------------------------------------------------------------- */

#define RTL169_IDR0         0x00
#define RTL169_MAR0         0x08
#define RTL169_DTCCR        0x10  /* dump tally counter command */
#define RTL169_TNPDS_LO    0x20  /* TX normal priority desc start low  */
#define RTL169_TNPDS_HI    0x24  /* TX normal priority desc start high */
#define RTL169_CR           0x37
#define RTL169_TPPOLL       0x38  /* transmit priority polling */
#define RTL169_IMR          0x3C
#define RTL169_ISR          0x3E
#define RTL169_TCR          0x40
#define RTL169_RCR          0x44
#define RTL169_RMS          0xDA  /* RX max packet size */
#define RTL169_CFG9346      0x50
#define RTL169_CONFIG1      0x52
#define RTL169_CONFIG2      0x53
#define RTL169_CONFIG3      0x54
#define RTL169_CONFIG4      0x55
#define RTL169_CONFIG5      0x56
#define RTL169_PHYSTATUS    0x6C  /* PHY status */
#define RTL169_RDSAR_LO    0xE4  /* RX desc start address low  */
#define RTL169_RDSAR_HI    0xE8  /* RX desc start address high */
#define RTL169_MTPS         0xEC  /* max TX packet size */

/* CR */
#define RTL169_CR_RST       (1 << 4)
#define RTL169_CR_RE        (1 << 3)
#define RTL169_CR_TE        (1 << 2)

/* TPPOLL */
#define RTL169_TPPOLL_NPQ   (1 << 6)  /* normal priority queue poll */

/* TCR */
#define RTL169_TCR_IFG_STD  (3u << 24)
#define RTL169_TCR_MXDMA    (7u << 8)

/* RCR */
#define RTL169_RCR_APM      (1u << 1)
#define RTL169_RCR_AB       (1u << 3)
#define RTL169_RCR_AM       (1u << 2)
#define RTL169_RCR_MXDMA    (7u << 8)

/* ISR */
#define RTL169_ISR_ROK      (1 << 0)
#define RTL169_ISR_TOK      (1 << 2)
#define RTL169_ISR_TER      (1 << 3)
#define RTL169_ISR_RER      (1 << 1)

/* PHY status */
#define RTL169_PHYS_LINK    (1 << 1)

/* Descriptor flags (shared by TX and RX descriptor rings) */
#define RTL169_DESC_OWN     (1u << 31)  /* owned by NIC */
#define RTL169_DESC_EOR     (1u << 30)  /* end of ring  */
#define RTL169_DESC_FS      (1u << 29)  /* first segment */
#define RTL169_DESC_LS      (1u << 28)  /* last segment  */
#define RTL169_DESC_LEN     0x3FFF      /* bits[13:0]    */

/* Descriptor ring sizes (must be power-of-two) */
#define RTL169_TX_DESCS     64
#define RTL169_RX_DESCS     256
#define RTL169_RX_BUF_SZ    2048        /* per RX descriptor */

/* CFG9346 */
#define RTL_CFG9346_UNLOCK  0xC0
#define RTL_CFG9346_LOCK    0x00

/* -----------------------------------------------------------------------
 * RTL8169 TX/RX descriptor (hardware layout, 16 bytes each)
 * ----------------------------------------------------------------------- */

typedef struct __attribute__((packed)) {
    volatile uint32_t opts1;   /* OWN|EOR|FS|LS|len */
    volatile uint32_t opts2;   /* VLAN / checksum offload */
    volatile uint64_t addr;    /* physical buffer address */
} RTL169Desc;

/* -----------------------------------------------------------------------
 * Unified driver state
 * ----------------------------------------------------------------------- */

typedef struct {
    /* -- chip identity -------------------------------------------------- */
    const RTLChipInfo *chip;     /* pointer into the chip table            */

    /* -- PCI location --------------------------------------------------- */
    uint8_t  pci_bus, pci_slot, pci_fn;

    /* -- address space -------------------------------------------------- */
    bool     use_mmio;           /* true = MMIO (8169), false = I/O (8139) */
    union {
        uint16_t iobase;         /* RTL8139: I/O port base                 */
        uint8_t *mmio;           /* RTL8169: MMIO virtual base             */
    };

    /* -- MAC -------------------------------------------------------------- */
    uint8_t  mac[6];
    bool     up;

    /* -- RTL8139 state (use_mmio == false) ------------------------------- */
    void    *tx139_bufs[RTL139_TX_SLOTS];
    uint32_t tx139_phys[RTL139_TX_SLOTS];
    uint8_t  tx139_slot;

    void    *rx139_buf;
    uint32_t rx139_phys;
    uint16_t rx139_offset;

    /* -- RTL8169 state (use_mmio == true) -------------------------------- */
    RTL169Desc *tx169_ring;      /* virtual                                */
    uint64_t    tx169_phys;      /* physical                               */
    void       *tx169_bufs[RTL169_TX_DESCS];
    uint64_t    tx169_buf_phys[RTL169_TX_DESCS];
    uint32_t    tx169_head;      /* next desc to fill                      */
    uint32_t    tx169_tail;      /* next desc to reclaim                   */

    RTL169Desc *rx169_ring;
    uint64_t    rx169_phys;
    void       *rx169_bufs[RTL169_RX_DESCS];
    uint64_t    rx169_buf_phys[RTL169_RX_DESCS];
    uint32_t    rx169_head;      /* next desc to read                      */
} RTLDev;

/* -----------------------------------------------------------------------
 * Public API
 * ----------------------------------------------------------------------- */

/*
 * Scan PCI, match against chip table, initialise hardware.
 * Returns true on success; dev is zeroed then filled in.
 */
bool rtl_init(RTLDev *dev);

/*
 * Transmit one raw Ethernet frame (already includes dst/src/ethertype).
 * Returns true on success, false on overflow or link down.
 */
bool rtl_send(RTLDev *dev, const void *data, uint16_t len);

/*
 * Poll for one received frame.  Copies into buf (up to buf_size bytes).
 * Returns actual frame length, or 0 if no frame is ready.
 */
uint16_t rtl_recv(RTLDev *dev, void *buf, uint16_t buf_size);

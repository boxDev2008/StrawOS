/*
 * rtl8139.c — Unified Realtek Fast/Gigabit Ethernet driver
 *
 * Supports the RTL8139 and RTL8169 families through a chip table, then
 * dispatches to either the legacy flat-buffer (8139) path or the
 * descriptor-ring (8169) path depending on what we found in PCI.
 *
 * Structure mirrors Linux drivers/net/ethernet/realtek/r8169_main.c:
 *   1. Chip table  – vendor:device → chip_id + capability flags + name
 *   2. Probe       – PCI scan, table match, BAR decode
 *   3. Init        – reset, MAC read, buffer alloc, ring setup
 *   4. TX / RX     – per-family implementations behind common wrappers
 */

#include "rtl.h"
#include "common.h"
#include "arch/x86_64/io.h"
#include "memory/pmm.h"
#include "libk/kprintf.h"
#include "libk/string.h"

/* -----------------------------------------------------------------------
 * Chip table  (analogous to Linux r8169_pci_tbl + rtl_chip_infos)
 *
 * subvendor/subdevice 0xFFFF = wildcard (match any).
 * Add rows as needed; probe() walks this table top-to-bottom and takes
 * the first match.
 * ----------------------------------------------------------------------- */

static const RTLChipInfo rtl_chip_table[] = {
    /*
     * ORDERING RULE: specific sub-IDs must come before wildcards (0xFFFF).
     * The probe walks top-to-bottom and takes the first match, so a wildcard
     * entry above a specific one would shadow the specific one entirely.
     */

    /* ---- RTL8139 family (I/O port, flat ring) ---- */
    /* Specific sub-IDs first */
    { 0x10EC, 0x8139, 0x1186, 0x1300, RTL_8139A,
      0,
      "RTL8139A (D-Link)" },
    { 0x10EC, 0x8139, 0x13D1, 0xAB06, RTL_8139AG,
      0,
      "RTL8139A/G" },
    { 0x10EC, 0x8139, 0x1259, 0xA117, RTL_8139C,
      0,
      "RTL8139C (Allied Telesyn)" },
    { 0x10EC, 0x8139, 0x144D, 0xC007, RTL_8100B_8139D,
      0,
      "RTL8139D / RTL8100B (Samsung)" },
    /* Wildcards after */
    { 0x10EC, 0x8138, 0xFFFF, 0xFFFF, RTL_8139B,
      0,
      "RTL8139B" },
    { 0x10EC, 0x8130, 0xFFFF, 0xFFFF, RTL_8130,
      0,
      "RTL8130" },
    { 0x10EC, 0x8100, 0xFFFF, 0xFFFF, RTL_8100,
      0,
      "RTL8100" },
    { 0x10EC, 0x8139, 0xFFFF, 0xFFFF, RTL_8139CP,
      0,
      "RTL8139C+ (generic)" },
    { 0x10EC, 0x8101, 0xFFFF, 0xFFFF, RTL_8101L,
      0,
      "RTL8101L" },

    /* ---- RTL8169 family (MMIO, descriptor rings) ---- */
    { 0x10EC, 0x8169, 0xFFFF, 0xFFFF, RTL_8169,
      RTL_CAP_MMIO | RTL_CAP_DESC_RING | RTL_CAP_JUMBO,
      "RTL8169" },
    { 0x10EC, 0x8169, 0x1458, 0xE000, RTL_8169S,
      RTL_CAP_MMIO | RTL_CAP_DESC_RING | RTL_CAP_JUMBO,
      "RTL8169S (Gigabyte)" },
    { 0x10EC, 0x8110, 0xFFFF, 0xFFFF, RTL_8110S,
      RTL_CAP_MMIO | RTL_CAP_DESC_RING | RTL_CAP_JUMBO,
      "RTL8110S" },
    { 0x10EC, 0x8169, 0x1043, 0x8B56, RTL_8169SB,
      RTL_CAP_MMIO | RTL_CAP_DESC_RING,
      "RTL8169SB (ASUS)" },
    { 0x10EC, 0x8167, 0xFFFF, 0xFFFF, RTL_8169SC_8110SC,
      RTL_CAP_MMIO | RTL_CAP_DESC_RING,
      "RTL8169SC/RTL8110SC" },
    { 0x10EC, 0x8168, 0xFFFF, 0xFFFF, RTL_8168B,
      RTL_CAP_MMIO | RTL_CAP_DESC_RING | RTL_CAP_64BIT_DMA | RTL_CAP_ASPM,
      "RTL8168B/RTL8111B" },
    { 0x10EC, 0x8168, 0x1043, 0x8B54, RTL_8168BB,
      RTL_CAP_MMIO | RTL_CAP_DESC_RING | RTL_CAP_64BIT_DMA | RTL_CAP_ASPM,
      "RTL8168BB (ASUS)" },
    { 0x10EC, 0x8168, 0x1458, 0xE000, RTL_8168C,
      RTL_CAP_MMIO | RTL_CAP_DESC_RING | RTL_CAP_64BIT_DMA | RTL_CAP_ASPM,
      "RTL8168C/RTL8111C (Gigabyte)" },
    { 0x10EC, 0x8168, 0x185B, 0xE000, RTL_8168CP,
      RTL_CAP_MMIO | RTL_CAP_DESC_RING | RTL_CAP_64BIT_DMA | RTL_CAP_ASPM,
      "RTL8168CP/RTL8111CP" },
    { 0x10EC, 0x8168, 0x1849, 0x8168, RTL_8168D,
      RTL_CAP_MMIO | RTL_CAP_DESC_RING | RTL_CAP_64BIT_DMA | RTL_CAP_ASPM,
      "RTL8168D/RTL8111D (ASRock)" },
    { 0x10EC, 0x8168, 0x10EC, 0x8168, RTL_8168DP,
      RTL_CAP_MMIO | RTL_CAP_DESC_RING | RTL_CAP_64BIT_DMA | RTL_CAP_ASPM,
      "RTL8168DP/RTL8111DP" },
    { 0x10EC, 0x8168, 0x1043, 0x8373, RTL_8168E,
      RTL_CAP_MMIO | RTL_CAP_DESC_RING | RTL_CAP_64BIT_DMA | RTL_CAP_ASPM,
      "RTL8168E/RTL8111E (ASUS)" },
    { 0x10EC, 0x8168, 0x1043, 0x8505, RTL_8168EP,
      RTL_CAP_MMIO | RTL_CAP_DESC_RING | RTL_CAP_64BIT_DMA | RTL_CAP_ASPM,
      "RTL8168EP/RTL8111EP (ASUS)" },
    { 0x10EC, 0x8168, 0x1462, 0x6131, RTL_8168F,
      RTL_CAP_MMIO | RTL_CAP_DESC_RING | RTL_CAP_64BIT_DMA | RTL_CAP_ASPM,
      "RTL8168F/RTL8111F (MSI)" },
    { 0x10EC, 0x8168, 0x1043, 0x8554, RTL_8168G,
      RTL_CAP_MMIO | RTL_CAP_DESC_RING | RTL_CAP_64BIT_DMA | RTL_CAP_ASPM | RTL_CAP_MSI,
      "RTL8168G/RTL8111G (ASUS)" },
    { 0x10EC, 0x8168, 0x1458, 0xE000, RTL_8168GU,
      RTL_CAP_MMIO | RTL_CAP_DESC_RING | RTL_CAP_64BIT_DMA | RTL_CAP_ASPM | RTL_CAP_MSI,
      "RTL8168GU/RTL8111GU (Gigabyte)" },
    { 0x10EC, 0x8168, 0x1043, 0x8672, RTL_8168H,
      RTL_CAP_MMIO | RTL_CAP_DESC_RING | RTL_CAP_64BIT_DMA | RTL_CAP_ASPM | RTL_CAP_MSI,
      "RTL8168H/RTL8111H (ASUS)" },
    /* Generic 8111 catch-alls for unlisted sub-IDs */
    { 0x10EC, 0x8168, 0xFFFF, 0xFFFF, RTL_8111H,
      RTL_CAP_MMIO | RTL_CAP_DESC_RING | RTL_CAP_64BIT_DMA | RTL_CAP_ASPM | RTL_CAP_MSI,
      "RTL8111H (generic)" },
    /* Fast Ethernet variants on 8169 architecture */
    { 0x10EC, 0x8136, 0xFFFF, 0xFFFF, RTL_8402,
      RTL_CAP_MMIO | RTL_CAP_DESC_RING | RTL_CAP_64BIT_DMA | RTL_CAP_ASPM,
      "RTL8402" },
    { 0x10EC, 0x8136, 0x1043, 0x84AA, RTL_8401,
      RTL_CAP_MMIO | RTL_CAP_DESC_RING | RTL_CAP_64BIT_DMA | RTL_CAP_ASPM,
      "RTL8401 (ASUS)" },
    { 0x10EC, 0x8136, 0x17AA, 0x3815, RTL_8103E,
      RTL_CAP_MMIO | RTL_CAP_DESC_RING | RTL_CAP_64BIT_DMA | RTL_CAP_ASPM,
      "RTL8103E (Lenovo)" },
    { 0x10EC, 0x8136, 0x1043, 0x84B8, RTL_8102E,
      RTL_CAP_MMIO | RTL_CAP_DESC_RING | RTL_CAP_64BIT_DMA | RTL_CAP_ASPM,
      "RTL8102E (ASUS)" },
    { 0x10EC, 0x8136, 0xFFFF, 0xFFFF, RTL_8101E,
      RTL_CAP_MMIO | RTL_CAP_DESC_RING | RTL_CAP_64BIT_DMA | RTL_CAP_ASPM,
      "RTL8101E" },
    { 0x10EC, 0x8129, 0xFFFF, 0xFFFF, RTL_8100E,
      RTL_CAP_MMIO | RTL_CAP_DESC_RING,
      "RTL8100E" },

    /* Sentinel */
    { 0, 0, 0, 0, RTL_8139, 0, NULL }
};

/* -----------------------------------------------------------------------
 * PCI helpers (brute-force config-space scan)
 * ----------------------------------------------------------------------- */

#define PCI_CONFIG_ADDRESS  0xCF8
#define PCI_CONFIG_DATA     0xCFC

#define PCI_VENDOR_ID       0x00
#define PCI_DEVICE_ID       0x02
#define PCI_COMMAND         0x04
#define PCI_HEADER_TYPE     0x0E
#define PCI_SUBVENDOR_ID    0x2C
#define PCI_SUBDEVICE_ID    0x2E
#define PCI_BAR0            0x10
#define PCI_BAR1            0x14

#define PCI_CMD_IO          (1 << 0)
#define PCI_CMD_MMIO        (1 << 1)
#define PCI_CMD_BUSMASTER   (1 << 2)

static uint32_t pci_read32(uint8_t bus, uint8_t slot, uint8_t fn, uint8_t off)
{
    outl(PCI_CONFIG_ADDRESS,
         (1u<<31)|((uint32_t)bus<<16)|((uint32_t)slot<<11)|((uint32_t)fn<<8)|(off&0xFC));
    return inl(PCI_CONFIG_DATA);
}

static void pci_write32(uint8_t bus, uint8_t slot, uint8_t fn,
                        uint8_t off, uint32_t val)
{
    outl(PCI_CONFIG_ADDRESS,
         (1u<<31)|((uint32_t)bus<<16)|((uint32_t)slot<<11)|((uint32_t)fn<<8)|(off&0xFC));
    outl(PCI_CONFIG_DATA, val);
}

static uint16_t pci_read16(uint8_t bus, uint8_t slot, uint8_t fn, uint8_t off)
{
    return (uint16_t)(pci_read32(bus, slot, fn, off & ~3) >> ((off & 2) * 8));
}

static void pci_write16(uint8_t bus, uint8_t slot, uint8_t fn,
                        uint8_t off, uint16_t val)
{
    uint32_t v = pci_read32(bus, slot, fn, off & ~3);
    int sh = (off & 2) * 8;
    v &= ~(0xFFFFu << sh);
    v |= (uint32_t)val << sh;
    pci_write32(bus, slot, fn, off & ~3, v);
}

/* -----------------------------------------------------------------------
 * RTL8139 I/O-port accessors
 * ----------------------------------------------------------------------- */

static inline uint8_t  r8_139 (RTLDev *d, uint8_t reg){ return inb (d->iobase + reg); }
static inline uint16_t r16_139(RTLDev *d, uint8_t reg){ return inw (d->iobase + reg); }
static inline uint32_t r32_139(RTLDev *d, uint8_t reg){ return inl (d->iobase + reg); }
static inline void     w8_139 (RTLDev *d, uint8_t reg, uint8_t  v){ outb(d->iobase + reg, v); }
static inline void     w16_139(RTLDev *d, uint8_t reg, uint16_t v){ outw(d->iobase + reg, v); }
static inline void     w32_139(RTLDev *d, uint8_t reg, uint32_t v){ outl(d->iobase + reg, v); }

/* -----------------------------------------------------------------------
 * RTL8169 MMIO accessors
 * ----------------------------------------------------------------------- */

static inline uint8_t  r8_169 (RTLDev *d, uint8_t reg){ return *(volatile uint8_t  *)(d->mmio + reg); }
static inline uint16_t r16_169(RTLDev *d, uint8_t reg){ return *(volatile uint16_t *)(d->mmio + reg); }
static inline uint32_t r32_169(RTLDev *d, uint8_t reg){ return *(volatile uint32_t *)(d->mmio + reg); }
static inline void     w8_169 (RTLDev *d, uint8_t reg, uint8_t  v){ *(volatile uint8_t  *)(d->mmio + reg) = v; }
static inline void     w16_169(RTLDev *d, uint8_t reg, uint16_t v){ *(volatile uint16_t *)(d->mmio + reg) = v; }
static inline void     w32_169(RTLDev *d, uint8_t reg, uint32_t v){ *(volatile uint32_t *)(d->mmio + reg) = v; }

/* -----------------------------------------------------------------------
 * DMA allocation helpers
 * ----------------------------------------------------------------------- */

/* Allocate physically contiguous pages; *virt_out → virtual mapping.
 * Returns physical address, or 0 on failure.
 * If below32 is true, we reject allocations above 4 GiB (for RTL8139). */
static uint64_t dma_alloc(uint32_t pages, void **virt_out, bool below32)
{
    uint32_t order = 0, n = 1;
    while (n < pages) { n <<= 1; order++; }

    uint64_t phys = pmm_alloc(order);
    if (!phys) return 0;

    if (below32 && phys > 0xFFFFFFFFULL) {
        pmm_free(phys, order);
        kprintf("[rtl] DMA alloc above 4 GiB — not usable for 32-bit chip\r\n");
        return 0;
    }

    *virt_out = PHYS_TO_VIRT(phys);
    memset(*virt_out, 0, (size_t)n * PAGE_SIZE);
    return phys;
}

/* Convenience wrappers */
static uint32_t dma_alloc32(uint32_t pages, void **virt_out)
{
    return (uint32_t)dma_alloc(pages, virt_out, true);
}

static uint64_t dma_alloc64(uint32_t pages, void **virt_out)
{
    return dma_alloc(pages, virt_out, false);
}

/* -----------------------------------------------------------------------
 * Chip table lookup
 * ----------------------------------------------------------------------- */

static const RTLChipInfo *rtl_match_chip(uint16_t vid, uint16_t did,
                                          uint16_t svid, uint16_t sdid)
{
    for (const RTLChipInfo *c = rtl_chip_table; c->name != NULL; c++) {
        if (c->vendor_id != vid || c->device_id != did)
            continue;
        /* Sub-IDs: 0xFFFF means wildcard */
        if (c->subvendor != 0xFFFF && c->subvendor != svid)
            continue;
        if (c->subdevice != 0xFFFF && c->subdevice != sdid)
            continue;
        return c;
    }
    return NULL;
}

/* -----------------------------------------------------------------------
 * PCI probe: scan, match chip, decode BAR
 * ----------------------------------------------------------------------- */

static bool rtl_pci_probe(RTLDev *dev)
{
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t slot = 0; slot < 32; slot++) {
            uint8_t hdr = (uint8_t)(pci_read32(bus, slot, 0, PCI_HEADER_TYPE) >> 16);
            uint8_t max_fn = (hdr & 0x80) ? 8 : 1;
            for (uint8_t fn = 0; fn < max_fn; fn++) {
                uint32_t id  = pci_read32(bus, slot, fn, 0);
                uint16_t vid = (uint16_t)(id & 0xFFFF);
                uint16_t did = (uint16_t)(id >> 16);
                if (vid == 0xFFFF) continue;

                uint32_t sub = pci_read32(bus, slot, fn, PCI_SUBVENDOR_ID);
                uint16_t svid = (uint16_t)(sub & 0xFFFF);
                uint16_t sdid = (uint16_t)(sub >> 16);

                const RTLChipInfo *chip = rtl_match_chip(vid, did, svid, sdid);
                if (!chip) continue;

                kprintf("[rtl] Found %s  [%04x:%04x sub %04x:%04x]  at %02x:%02x.%x\r\n",
                        chip->name, vid, did, svid, sdid, bus, slot, fn);

                dev->chip     = chip;
                dev->pci_bus  = (uint8_t)bus;
                dev->pci_slot = slot;
                dev->pci_fn   = fn;

                /* ---- decode address space ---- */
                if (chip->caps & RTL_CAP_MMIO) {
                    /*
                     * RTL8169 BAR layout (per datasheet and Linux r8169):
                     *   BAR0 (offset 0x10) = 256-byte I/O port space
                     *   BAR1 (offset 0x14) = 256-byte 32-bit MMIO
                     *
                     * We always use MMIO (BAR1).  BAR1 is a plain 32-bit
                     * memory BAR — bits[3:0] will be 0b0000 (non-prefetch,
                     * 32-bit, memory type).  There is no 64-bit BAR here.
                     *
                     * On a handful of early boards BAR1 may not be decoded
                     * and will read back 0; in that case fall back to BAR0
                     * treated as MMIO (some BIOSes map it that way).
                     */
                    uint32_t bar1 = pci_read32(bus, slot, fn, PCI_BAR1);
                    uint64_t mmio_phys;
                    if ((bar1 & 1) == 0 && (bar1 & ~0xFU) != 0) {
                        /* Normal case: BAR1 is a memory BAR with a valid address */
                        mmio_phys = bar1 & ~0xFULL;
                    } else {
                        /* Fallback: try BAR0 as MMIO (some early 8169 boards) */
                        uint32_t bar0 = pci_read32(bus, slot, fn, PCI_BAR0);
                        if (bar0 & 1) {
                            kprintf("[rtl] 8169 neither BAR0 nor BAR1 looks like MMIO "
                                    "(bar0=0x%08x bar1=0x%08x)\r\n", bar0, bar1);
                            return false;
                        }
                        mmio_phys = bar0 & ~0xFULL;
                        kprintf("[rtl] 8169 using BAR0 as MMIO fallback\r\n");
                    }
                    dev->mmio     = PHYS_TO_VIRT(mmio_phys);
                    dev->use_mmio = true;
                } else {
                    /* RTL8139: BAR0 is I/O port */
                    uint32_t bar0 = pci_read32(bus, slot, fn, PCI_BAR0);
                    if (!(bar0 & 1)) {
                        kprintf("[rtl] BAR0 expected I/O for 8139 but got MMIO\r\n");
                        return false;
                    }
                    dev->iobase   = (uint16_t)(bar0 & ~3u);
                    dev->use_mmio = false;
                }

                /* ---- enable I/O + MMIO + bus-master ---- */
                uint16_t cmd = pci_read16(bus, slot, fn, PCI_COMMAND);
                cmd |= PCI_CMD_IO | PCI_CMD_MMIO | PCI_CMD_BUSMASTER;
                pci_write16(bus, slot, fn, PCI_COMMAND, cmd);

                return true;
            }
        }
    }
    kprintf("[rtl] No supported Realtek NIC found\r\n");
    return false;
}

/* -----------------------------------------------------------------------
 * RTL8139 init path
 * ----------------------------------------------------------------------- */

static bool rtl139_hw_init(RTLDev *dev)
{
    /* Power on */
    w8_139(dev, RTL139_CONFIG1, 0x00);

    /* Software reset */
    w8_139(dev, RTL139_CR, RTL139_CR_RST);
    for (int i = 0; i < 100000; i++) {
        if (!(r8_139(dev, RTL139_CR) & RTL139_CR_RST)) break;
        __asm__ volatile("pause");
    }
    if (r8_139(dev, RTL139_CR) & RTL139_CR_RST) {
        kprintf("[rtl] 8139 reset timed out\r\n");
        return false;
    }
    kprintf("[rtl] 8139 reset OK, I/O base=0x%04x\r\n", dev->iobase);

    /* MAC */
    for (int i = 0; i < 6; i++)
        dev->mac[i] = r8_139(dev, RTL139_IDR0 + i);

    /* TX buffers */
    for (int i = 0; i < RTL139_TX_SLOTS; i++) {
        dev->tx139_phys[i] = dma_alloc32(1, &dev->tx139_bufs[i]);
        if (!dev->tx139_phys[i]) return false;
    }

    /* RX ring (needs 64K + 16 + 1500 ≈ 17 pages → order 5 = 32 pages) */
    dev->rx139_phys = dma_alloc32(32, &dev->rx139_buf);
    if (!dev->rx139_phys) return false;

    w32_139(dev, RTL139_RBSTART, dev->rx139_phys);

    static const uint8_t tsad_reg[4] = { RTL139_TSAD0, RTL139_TSAD1,
                                          RTL139_TSAD2, RTL139_TSAD3 };
    for (int i = 0; i < RTL139_TX_SLOTS; i++)
        w32_139(dev, tsad_reg[i], dev->tx139_phys[i]);

    /* Mask all interrupts (polled driver) */
    w16_139(dev, RTL139_IMR, 0x0000);
    w16_139(dev, RTL139_ISR, 0xFFFF);

    /* TX config */
    w32_139(dev, RTL139_TCR, RTL139_TCR_IFG_STD | RTL139_TCR_MXDMA);

    /* RX config: accept physical + broadcast + multicast, 64K ring, wrap */
    w32_139(dev, RTL139_RCR,
            RTL139_RCR_APM | RTL139_RCR_AB | RTL139_RCR_AM |
            RTL139_RCR_WRAP | RTL139_RCR_MXDMA |
            RTL139_RCR_RBLEN64K | RTL139_RCR_RXFTH);

    /* Accept all multicast */
    w32_139(dev, RTL139_MAR0, 0xFFFFFFFF);
    w32_139(dev, RTL139_MAR4, 0xFFFFFFFF);

    /* Enable TX + RX */
    w8_139(dev, RTL139_CR, RTL139_CR_TE | RTL139_CR_RE);

    dev->rx139_offset = 0;
    dev->tx139_slot   = 0;

    /* Link: MSR bit2 = LINKB (inverted on some revisions; QEMU = UP) */
    uint8_t msr = r8_139(dev, RTL139_MSR);
    dev->up = !(msr & (1 << 2));
    if (!dev->up) {
        dev->up = true;   /* QEMU quirk */
        kprintf("[rtl] 8139 link status ambiguous (MSR=0x%02x), assuming UP\r\n", msr);
    } else {
        kprintf("[rtl] 8139 link UP (MSR=0x%02x)\r\n", msr);
    }

    return true;
}

/* -----------------------------------------------------------------------
 * RTL8169 init path
 * ----------------------------------------------------------------------- */

static bool rtl169_hw_init(RTLDev *dev)
{
    /* Unlock registers */
    w8_169(dev, RTL169_CFG9346, RTL_CFG9346_UNLOCK);

    /* Software reset */
    w8_169(dev, RTL169_CR, RTL169_CR_RST);
    for (int i = 0; i < 100000; i++) {
        if (!(r8_169(dev, RTL169_CR) & RTL169_CR_RST)) break;
        __asm__ volatile("pause");
    }
    if (r8_169(dev, RTL169_CR) & RTL169_CR_RST) {
        kprintf("[rtl] 8169 reset timed out\r\n");
        w8_169(dev, RTL169_CFG9346, RTL_CFG9346_LOCK);
        return false;
    }
    kprintf("[rtl] 8169 reset OK, MMIO=%p\r\n", (void *)dev->mmio);

    /* MAC */
    for (int i = 0; i < 6; i++)
        dev->mac[i] = r8_169(dev, RTL169_IDR0 + i);

    /* ---- Allocate TX descriptor ring ---- */
    {
        uint32_t ring_pages = (RTL169_TX_DESCS * sizeof(RTL169Desc) + PAGE_SIZE - 1) / PAGE_SIZE;
        void *virt;
        dev->tx169_phys = dma_alloc64(ring_pages, &virt);
        if (!dev->tx169_phys) return false;
        dev->tx169_ring = (RTL169Desc *)virt;

        /* Allocate one TX buffer per descriptor */
        for (int i = 0; i < RTL169_TX_DESCS; i++) {
            dev->tx169_buf_phys[i] = dma_alloc64(1, &dev->tx169_bufs[i]);
            if (!dev->tx169_buf_phys[i]) return false;
            dev->tx169_ring[i].addr  = dev->tx169_buf_phys[i];
            dev->tx169_ring[i].opts1 = 0;   /* CPU-owned initially */
            dev->tx169_ring[i].opts2 = 0;
        }
        /* Mark last descriptor as end-of-ring */
        dev->tx169_ring[RTL169_TX_DESCS - 1].opts1 |= RTL169_DESC_EOR;
    }

    /* ---- Allocate RX descriptor ring ---- */
    {
        uint32_t ring_pages = (RTL169_RX_DESCS * sizeof(RTL169Desc) + PAGE_SIZE - 1) / PAGE_SIZE;
        void *virt;
        dev->rx169_phys = dma_alloc64(ring_pages, &virt);
        if (!dev->rx169_phys) return false;
        dev->rx169_ring = (RTL169Desc *)virt;

        for (int i = 0; i < RTL169_RX_DESCS; i++) {
            dev->rx169_buf_phys[i] = dma_alloc64(1, &dev->rx169_bufs[i]);
            if (!dev->rx169_buf_phys[i]) return false;
            uint32_t opts1 = RTL169_DESC_OWN | RTL169_RX_BUF_SZ;
            if (i == RTL169_RX_DESCS - 1) opts1 |= RTL169_DESC_EOR;
            dev->rx169_ring[i].opts1 = opts1;
            dev->rx169_ring[i].opts2 = 0;
            dev->rx169_ring[i].addr  = dev->rx169_buf_phys[i];
        }
    }

    /* ---- Program descriptor ring base addresses ---- */
    w32_169(dev, RTL169_TNPDS_LO, (uint32_t)(dev->tx169_phys & 0xFFFFFFFF));
    w32_169(dev, RTL169_TNPDS_HI, (uint32_t)(dev->tx169_phys >> 32));
    w32_169(dev, RTL169_RDSAR_LO, (uint32_t)(dev->rx169_phys & 0xFFFFFFFF));
    w32_169(dev, RTL169_RDSAR_HI, (uint32_t)(dev->rx169_phys >> 32));

    /* ---- Misc chip setup ---- */

    /* Max RX packet size (includes FCS) */
    w16_169(dev, RTL169_RMS, 1536);

    /*
     * Max TX packet size (MTPS).
     * On 8168+ the unit is 128 bytes.  0x0C × 128 = 1536, which covers
     * a standard 1500-byte MTU frame plus the 14-byte Ethernet header
     * and 4-byte FCS.  Earlier 8169 chips ignore this register entirely,
     * so writing it is harmless.
     */
    w8_169(dev, RTL169_MTPS, 0x0C);

    /* TX config: standard IFG, max DMA burst */
    w32_169(dev, RTL169_TCR, RTL169_TCR_IFG_STD | RTL169_TCR_MXDMA);

    /* RX config: accept physical + broadcast + multicast, max DMA burst */
    w32_169(dev, RTL169_RCR,
            RTL169_RCR_APM | RTL169_RCR_AB | RTL169_RCR_AM | RTL169_RCR_MXDMA);

    /* Accept all multicast */
    w32_169(dev, RTL169_MAR0, 0xFFFFFFFF);
    w32_169(dev, RTL169_MAR0 + 4, 0xFFFFFFFF);

    /* Mask all interrupts (polled) */
    w16_169(dev, RTL169_IMR, 0x0000);
    w16_169(dev, RTL169_ISR, 0xFFFF);

    /* Lock registers and enable TX+RX */
    w8_169(dev, RTL169_CFG9346, RTL_CFG9346_LOCK);
    w8_169(dev, RTL169_CR, RTL169_CR_TE | RTL169_CR_RE);

    dev->tx169_head = dev->tx169_tail = 0;
    dev->rx169_head = 0;

    /* Link */
    uint8_t phys = r8_169(dev, RTL169_PHYSTATUS);
    dev->up = !!(phys & RTL169_PHYS_LINK);
    if (!dev->up) {
        dev->up = true;   /* assume UP for QEMU */
        kprintf("[rtl] 8169 link status ambiguous (PHYSTATUS=0x%02x), assuming UP\r\n", phys);
    } else {
        kprintf("[rtl] 8169 link UP (PHYSTATUS=0x%02x)\r\n", phys);
    }

    return true;
}

/* -----------------------------------------------------------------------
 * Public: rtl_init
 * ----------------------------------------------------------------------- */

bool rtl_init(RTLDev *dev)
{
    memset(dev, 0, sizeof *dev);

    if (!rtl_pci_probe(dev))
        return false;

    bool ok = dev->use_mmio ? rtl169_hw_init(dev) : rtl139_hw_init(dev);
    if (!ok) return false;

    kprintf("[rtl] %s initialised — MAC: %02x:%02x:%02x:%02x:%02x:%02x\r\n",
            dev->chip->name,
            dev->mac[0], dev->mac[1], dev->mac[2],
            dev->mac[3], dev->mac[4], dev->mac[5]);
    return true;
}

/* -----------------------------------------------------------------------
 * RTL8139 transmit
 * ----------------------------------------------------------------------- */

static const uint8_t tsd_reg[4]  = { 0x10, 0x14, 0x18, 0x1C };
static const uint8_t tsad_reg[4] = { 0x20, 0x24, 0x28, 0x2C };

static bool rtl139_send(RTLDev *dev, const void *data, uint16_t len)
{
    if (!dev->up || len > 1792) return false;

    uint8_t slot = dev->tx139_slot;

    /* Wait for the slot to be free */
    for (int i = 0; i < 100000; i++) {
        if (r32_139(dev, tsd_reg[slot]) & RTL139_TSD_OWN) break;
        __asm__ volatile("pause");
    }

    memcpy(dev->tx139_bufs[slot], data, len);
    if (len < 60) {
        memset((uint8_t *)dev->tx139_bufs[slot] + len, 0, 60 - len);
        len = 60;
    }

    w32_139(dev, tsad_reg[slot], dev->tx139_phys[slot]);
    __asm__ volatile("" ::: "memory");
    w32_139(dev, tsd_reg[slot], (uint32_t)len & RTL139_TSD_SIZE);

    dev->tx139_slot = (slot + 1) % RTL139_TX_SLOTS;
    return true;
}

/* -----------------------------------------------------------------------
 * RTL8139 receive
 * ----------------------------------------------------------------------- */

static uint16_t rtl139_recv(RTLDev *dev, void *buf, uint16_t buf_size)
{
    w16_139(dev, RTL139_ISR,
            RTL139_ISR_ROK | RTL139_ISR_RXOVW | RTL139_ISR_FOVW);

    if (r8_139(dev, RTL139_CR) & RTL139_CR_BUFE)
        return 0;

    uint8_t *ring = (uint8_t *)dev->rx139_buf;
    uint16_t status  = *(uint16_t *)(ring + dev->rx139_offset);
    uint16_t pkt_len = *(uint16_t *)(ring + dev->rx139_offset + 2);

    if (!(status & RTL139_RX_ROK) || (status & RTL139_RX_ERR_MASK)) {
        kprintf("[rtl] 8139 RX error status=0x%04x\r\n", status);
        uint16_t cbr = r16_139(dev, RTL139_CBR);
        dev->rx139_offset = cbr;
        w16_139(dev, RTL139_CAPR, (uint16_t)(cbr - 16));
        return 0;
    }

    uint16_t data_len = pkt_len - 4;   /* strip hardware CRC */
    uint16_t copy_len = (data_len < buf_size) ? data_len : buf_size;
    uint16_t data_off = (dev->rx139_offset + 4) % (64 * 1024);

    if (data_off + copy_len <= (64 * 1024 + 1500)) {
        memcpy(buf, ring + data_off, copy_len);
    } else {
        uint16_t first = (uint16_t)((64 * 1024) - data_off);
        memcpy(buf, ring + data_off, first);
        memcpy((uint8_t *)buf + first, ring, copy_len - first);
    }

    dev->rx139_offset =
        (uint16_t)((dev->rx139_offset + 4 + pkt_len + 3) & ~3u) % (64 * 1024);
    w16_139(dev, RTL139_CAPR, (uint16_t)(dev->rx139_offset - 16));

    return data_len;
}

/* -----------------------------------------------------------------------
 * RTL8169 transmit
 * ----------------------------------------------------------------------- */

static bool rtl169_send(RTLDev *dev, const void *data, uint16_t len)
{
    if (!dev->up || len > 1518) return false;

    uint32_t idx = dev->tx169_head;
    RTL169Desc *desc = &dev->tx169_ring[idx];

    /* Wait for descriptor to be free (CPU-owned = OWN cleared) */
    for (int i = 0; i < 100000; i++) {
        if (!(desc->opts1 & RTL169_DESC_OWN)) break;
        __asm__ volatile("pause");
    }
    if (desc->opts1 & RTL169_DESC_OWN) {
        kprintf("[rtl] 8169 TX ring full\r\n");
        return false;
    }

    if (len < 60) {
        memset((uint8_t *)dev->tx169_bufs[idx] + len, 0, 60 - len);
        len = 60;
    }
    memcpy(dev->tx169_bufs[idx], data, len);

    uint32_t opts1 = RTL169_DESC_OWN | RTL169_DESC_FS | RTL169_DESC_LS
                   | (uint32_t)len;
    if (idx == RTL169_TX_DESCS - 1)
        opts1 |= RTL169_DESC_EOR;

    desc->opts2 = 0;
    desc->addr  = dev->tx169_buf_phys[idx];

    __asm__ volatile("" ::: "memory");
    desc->opts1 = opts1;
    __asm__ volatile("" ::: "memory");

    /* Kick the NIC */
    w8_169(dev, RTL169_TPPOLL, RTL169_TPPOLL_NPQ);

    dev->tx169_head = (idx + 1) % RTL169_TX_DESCS;
    return true;
}

/* -----------------------------------------------------------------------
 * RTL8169 receive
 * ----------------------------------------------------------------------- */

static uint16_t rtl169_recv(RTLDev *dev, void *buf, uint16_t buf_size)
{
    uint32_t idx = dev->rx169_head;
    RTL169Desc *desc = &dev->rx169_ring[idx];

    /* If NIC still owns the descriptor, no packet yet */
    if (desc->opts1 & RTL169_DESC_OWN)
        return 0;

    /* Clear ISR ROK (polled, but keeps the flag from accumulating) */
    w16_169(dev, RTL169_ISR, RTL169_ISR_ROK);

    uint32_t opts1   = desc->opts1;
    uint16_t pkt_len = (uint16_t)(opts1 & RTL169_DESC_LEN);
    uint16_t data_len = 0;

    /* Sanity: expect FS+LS set (single-descriptor frames only for now) */
    if (!(opts1 & RTL169_DESC_FS) || !(opts1 & RTL169_DESC_LS)) {
        kprintf("[rtl] 8169 multi-segment RX not supported, discarding\r\n");
    } else if (pkt_len > 4) {
        data_len = pkt_len - 4;   /* strip 4-byte hardware CRC */
        uint16_t copy_len = (data_len < buf_size) ? data_len : buf_size;
        memcpy(buf, dev->rx169_bufs[idx], copy_len);
    }

    /* Recycle descriptor back to NIC unconditionally */
    uint32_t new_opts1 = RTL169_DESC_OWN | RTL169_RX_BUF_SZ;
    if (idx == RTL169_RX_DESCS - 1) new_opts1 |= RTL169_DESC_EOR;
    desc->opts2 = 0;
    desc->addr  = dev->rx169_buf_phys[idx];
    __asm__ volatile("" ::: "memory");
    desc->opts1 = new_opts1;

    dev->rx169_head = (idx + 1) % RTL169_RX_DESCS;
    return data_len;
}

/* -----------------------------------------------------------------------
 * Public: rtl_send / rtl_recv  (dispatch on family)
 * ----------------------------------------------------------------------- */

bool rtl_send(RTLDev *dev, const void *data, uint16_t len)
{
    if (dev->use_mmio)
        return rtl169_send(dev, data, len);
    else
        return rtl139_send(dev, data, len);
}

uint16_t rtl_recv(RTLDev *dev, void *buf, uint16_t buf_size)
{
    if (dev->use_mmio)
        return rtl169_recv(dev, buf, buf_size);
    else
        return rtl139_recv(dev, buf, buf_size);
}

#define R69_REG_MAC0        0x00
#define R69_REG_MAR0        0x08
#define R69_REG_TNPDS_LO    0x20
#define R69_REG_TNPDS_HI    0x24
#define R69_REG_CMD         0x37
#define R69_REG_TXPOLL      0x38
#define R69_REG_IMR         0x3C
#define R69_REG_ISR         0x3E
#define R69_REG_TXCONFIG    0x40
#define R69_REG_RXCONFIG    0x44
#define R69_REG_CFG9346     0x50
#define R69_REG_PHYSTATUS   0x6C
#define R69_REG_MCU         0xD3
#define R69_REG_RXMAXSIZE   0xDA
#define R69_REG_CPLUSCMD    0xE0
#define R69_REG_RDSAR_LO    0xE4
#define R69_REG_RDSAR_HI    0xE8
#define R69_REG_MAXTXPKT    0xEC
#define R69_REG_MISC        0xF0

#define R69_CMD_RESET       (1 << 4)
#define R69_CMD_RX_ENABLE   (1 << 3)
#define R69_CMD_TX_ENABLE   (1 << 2)

#define R69_TXPOLL_NPQ      (1 << 6)

#define R69_CFG9346_UNLOCK  0xC0
#define R69_CFG9346_LOCK    0x00

#define R69_RX_ACCEPT_MASK  0x3F
#define R69_RX_ACCEPT_BCAST (1 << 3)
#define R69_RX_ACCEPT_MCAST (1 << 2)
#define R69_RX_ACCEPT_MYPHYS (1 << 1)
#define R69_RX_FIFO_THRESH  (7 << 13)
#define R69_RX_128_INT_EN   (1 << 15)
#define R69_RX_MULTI_EN     (1 << 14)
#define R69_RX_EARLY_OFF    (1 << 11)
#define R69_RX_DMA_BURST    (7 << 8)

#define R69_TX_IFG96        (3 << 24)
#define R69_TX_DMA_BURST    (7 << 8)
#define R69_TX_AUTO_FIFO    (1 << 7)

#define R69_PHY_LINK        (1 << 1)
#define R69_PHY_FULLDUP     (1 << 0)
#define R69_PHY_10M         (1 << 2)
#define R69_PHY_100M        (1 << 3)
#define R69_PHY_1000M       (1 << 4)

#define R69_MCU_NOW_IS_OOB  (1 << 7)
#define R69_MISC_RXDV_GATE  (1 << 19)

#define R69_CPCMD_PCIDAC    (1 << 4)

#define R69_DESC_OWN        (1u << 31)
#define R69_DESC_EOR        (1u << 30)
#define R69_DESC_FS         (1u << 29)
#define R69_DESC_LS         (1u << 28)
#define R69_RXDESC_RES      (1u << 21)
#define R69_DESC_LEN_MASK   0x3FFF

#define R69_NUM_RX_DESC     64
#define R69_NUM_TX_DESC     16
#define R69_BUF_SIZE        2048

typedef struct
{
    volatile uint32_t opts1;
    volatile uint32_t opts2;
    volatile uint64_t addr;
}
Rtl8169Desc;

typedef struct
{
    volatile uint8_t *mmio;

    uint8_t  mac[6];
    int      old_8169;
    int      evl_up;
    int      g_up;

    Rtl8169Desc *rx_ring;
    Rtl8169Desc *tx_ring;
    uint8_t     *rx_buf[R69_NUM_RX_DESC];
    uint8_t     *tx_buf[R69_NUM_TX_DESC];

    uint32_t rx_next;
    uint32_t tx_next;

    Rtl8169RxCallback rx_callback;
}
Rtl8169Device;

static Rtl8169Device rtl8169;
static bool rtl8169_ready;

static const uint16_t rtl8169_device_ids[] = {
    0x8161, 0x8167, 0x8168, 0x8169, 0x8136, 0x8129, 0x8162, 0x2502, 0x2600,
};

#define r69_barrier() __asm__ volatile("" ::: "memory")

static uint8_t  r69_in8(uint32_t reg)  { return *(volatile uint8_t  *)(rtl8169.mmio + reg); }
static uint16_t r69_in16(uint32_t reg) { return *(volatile uint16_t *)(rtl8169.mmio + reg); }
static uint32_t r69_in32(uint32_t reg) { return *(volatile uint32_t *)(rtl8169.mmio + reg); }

static void r69_out8(uint32_t reg, uint8_t val)   { *(volatile uint8_t  *)(rtl8169.mmio + reg) = val; }
static void r69_out16(uint32_t reg, uint16_t val) { *(volatile uint16_t *)(rtl8169.mmio + reg) = val; }
static void r69_out32(uint32_t reg, uint32_t val) { *(volatile uint32_t *)(rtl8169.mmio + reg) = val; }

typedef struct
{
    PciDevice *out;
    int found;
}
Rtl8169FindCtx;

static void rtl8169_find_cb(const PciDevice *dev, void *ctx_)
{
    Rtl8169FindCtx *ctx = (Rtl8169FindCtx *)ctx_;
    if (ctx->found || dev->vendor_id != RTL8169_VENDOR_ID)
        return;
    for (size_t i = 0; i < ARRAY_LEN(rtl8169_device_ids); i++)
    {
        if (dev->device_id == rtl8169_device_ids[i])
        {
            *ctx->out = *dev;
            ctx->found = 1;
            return;
        }
    }
}

static void rtl8169_classify(uint32_t xid)
{
    static const uint16_t g_up_xids[] = {
        0x4c0, 0x4c1, 0x509, 0x5c8,
        0x540, 0x541, 0x6c0,
        0x502,
        0x54a, 0x54b,
    };
    static const uint16_t evl_xids[] = { 0x480, 0x481 };
    static const uint16_t evl_xids_7c8[] = { 0x2c8, 0x488 };

    for (size_t i = 0; i < ARRAY_LEN(g_up_xids); i++)
        if ((xid & 0x7cf) == g_up_xids[i])
            rtl8169.g_up = rtl8169.evl_up = 1;
    for (size_t i = 0; i < ARRAY_LEN(evl_xids); i++)
        if ((xid & 0x7cf) == evl_xids[i])
            rtl8169.evl_up = 1;
    for (size_t i = 0; i < ARRAY_LEN(evl_xids_7c8); i++)
        if ((xid & 0x7c8) == evl_xids_7c8[i])
            rtl8169.evl_up = 1;
}

static void rtl8169_rx_desc_reset(uint32_t i)
{
    Rtl8169Desc *d = &rtl8169.rx_ring[i];
    uint32_t eor = (i == R69_NUM_RX_DESC - 1) ? R69_DESC_EOR : 0;

    d->opts2 = 0;
    d->addr = virt_to_phys(rtl8169.rx_buf[i]);
    r69_barrier();
    d->opts1 = R69_DESC_OWN | eor | R69_BUF_SIZE;
}

void rtl8169_poll(void)
{
    if (!rtl8169_ready)
        return;

    uint16_t status = r69_in16(R69_REG_ISR);
    if (status)
        r69_out16(R69_REG_ISR, status);

    for (uint32_t budget = 0; budget < R69_NUM_RX_DESC; budget++)
    {
        uint32_t i = rtl8169.rx_next;
        uint32_t opts1 = rtl8169.rx_ring[i].opts1;

        if (opts1 & R69_DESC_OWN)
            break;
        r69_barrier();

        int whole = (opts1 & (R69_DESC_FS | R69_DESC_LS)) == (R69_DESC_FS | R69_DESC_LS);
        uint32_t len = opts1 & R69_DESC_LEN_MASK;

        if (whole && !(opts1 & R69_RXDESC_RES) && len > 4 && len <= R69_BUF_SIZE)
        {
            if (rtl8169.rx_callback)
                rtl8169.rx_callback(rtl8169.rx_buf[i], (uint16_t)(len - 4));
        }

        rtl8169_rx_desc_reset(i);
        rtl8169.rx_next = (i + 1) % R69_NUM_RX_DESC;
    }
}

void rtl8169_set_rx_callback(Rtl8169RxCallback cb)
{
    rtl8169.rx_callback = cb;
}

const uint8_t *rtl8169_mac(void)
{
    return rtl8169.mac;
}

void rtl8169_send(const uint8_t *frame, uint16_t len)
{
    if (!rtl8169_ready)
        return;
    if (len > R69_BUF_SIZE)
        len = R69_BUF_SIZE;

    uint32_t i = rtl8169.tx_next;
    Rtl8169Desc *d = &rtl8169.tx_ring[i];

    for (int spins = 0; d->opts1 & R69_DESC_OWN; spins++)
    {
        if (spins > 10000000)
        {
            kprintf("rtl8169: TX ring stuck, dropping frame\n");
            return;
        }
        pause();
    }

    uint8_t *buf = rtl8169.tx_buf[i];
    memcpy(buf, frame, len);
    if (len < 60)
    {
        memset(buf + len, 0, 60 - len);
        len = 60;
    }

    uint32_t eor = (i == R69_NUM_TX_DESC - 1) ? R69_DESC_EOR : 0;
    d->opts2 = 0;
    d->addr = virt_to_phys(buf);
    r69_barrier();
    d->opts1 = R69_DESC_OWN | eor | R69_DESC_FS | R69_DESC_LS | len;
    r69_barrier();

    rtl8169.tx_next = (i + 1) % R69_NUM_TX_DESC;
    r69_out8(R69_REG_TXPOLL, R69_TXPOLL_NPQ);
}

static int rtl8169_alloc_rings(void)
{
    rtl8169.rx_ring = (Rtl8169Desc *)pmm_alloc_page();
    rtl8169.tx_ring = (Rtl8169Desc *)pmm_alloc_page();
    if (!rtl8169.rx_ring || !rtl8169.tx_ring)
        return 0;
    memset(rtl8169.rx_ring, 0, PAGE_SIZE);
    memset(rtl8169.tx_ring, 0, PAGE_SIZE);

    for (uint32_t i = 0; i < R69_NUM_RX_DESC; i += 2)
    {
        uint8_t *page = (uint8_t *)pmm_alloc_page();
        if (!page)
            return 0;
        rtl8169.rx_buf[i] = page;
        rtl8169.rx_buf[i + 1] = page + R69_BUF_SIZE;
    }
    for (uint32_t i = 0; i < R69_NUM_TX_DESC; i += 2)
    {
        uint8_t *page = (uint8_t *)pmm_alloc_page();
        if (!page)
            return 0;
        rtl8169.tx_buf[i] = page;
        rtl8169.tx_buf[i + 1] = page + R69_BUF_SIZE;
    }

    for (uint32_t i = 0; i < R69_NUM_RX_DESC; i++)
        rtl8169_rx_desc_reset(i);
    rtl8169.tx_ring[R69_NUM_TX_DESC - 1].opts1 = R69_DESC_EOR;

    rtl8169.rx_next = 0;
    rtl8169.tx_next = 0;
    return 1;
}

static int rtl8169_dma_below_4g(void)
{
    if (virt_to_phys(rtl8169.rx_ring) >> 32 || virt_to_phys(rtl8169.tx_ring) >> 32)
        return 0;
    for (uint32_t i = 0; i < R69_NUM_RX_DESC; i++)
        if (virt_to_phys(rtl8169.rx_buf[i]) >> 32)
            return 0;
    for (uint32_t i = 0; i < R69_NUM_TX_DESC; i++)
        if (virt_to_phys(rtl8169.tx_buf[i]) >> 32)
            return 0;
    return 1;
}

int rtl8169_init(void)
{
    PciDevice dev;
    Rtl8169FindCtx ctx = { &dev, 0 };
    pci_enumerate(rtl8169_find_cb, &ctx);
    if (!ctx.found)
    {
        kprintf("rtl8169: no card found on PCI bus\n");
        return 0;
    }

    memset(&rtl8169, 0, sizeof(rtl8169));
    pci_enable_device(&dev);

    uint64_t mmio_phys = 0;
    for (int i = 0; i < PCI_MAX_BARS; i++)
    {
        if (dev.bar[i] && !pci_bar_is_io(dev.bar[i]))
        {
            mmio_phys = pci_bar_addr(&dev, i);
            break;
        }
    }
    if (!mmio_phys)
    {
        kprintf("rtl8169: no memory BAR\n");
        return 0;
    }
    rtl8169.mmio = (volatile uint8_t *)vmm_map_mmio(mmio_phys, PAGE_SIZE);

    uint32_t xid = (r69_in32(R69_REG_TXCONFIG) >> 20) & 0xFCF;
    rtl8169.old_8169 = dev.device_id == 0x8169 || dev.device_id == 0x8167;
    if (!rtl8169.old_8169)
        rtl8169_classify(xid);

    kprintf("rtl8169: found %x:%x (xid %x) at mmio %p\n",
            dev.vendor_id, dev.device_id, xid, (void *)mmio_phys);

    if (!rtl8169.old_8169 && (r69_in8(R69_REG_MCU) & R69_MCU_NOW_IS_OOB))
    {
        kprintf("rtl8169: leaving out-of-band mode\n");
        r69_out8(R69_REG_MCU, r69_in8(R69_REG_MCU) & ~R69_MCU_NOW_IS_OOB);
    }

    r69_out16(R69_REG_IMR, 0);
    r69_out16(R69_REG_ISR, 0xFFFF);

    r69_out8(R69_REG_CMD, R69_CMD_RESET);
    {
        int ms = 0;
        while (r69_in8(R69_REG_CMD) & R69_CMD_RESET)
        {
            if (++ms > 100)
            {
                kprintf("rtl8169: reset timed out\n");
                return 0;
            }
            task_sleep_ms(1);
        }
    }

    for (int i = 0; i < 6; i++)
        rtl8169.mac[i] = r69_in8(R69_REG_MAC0 + i);
    kprintf("rtl8169: MAC %x:%x:%x:%x:%x:%x\n",
            rtl8169.mac[0], rtl8169.mac[1], rtl8169.mac[2],
            rtl8169.mac[3], rtl8169.mac[4], rtl8169.mac[5]);

    if (!rtl8169_alloc_rings())
    {
        kprintf("rtl8169: failed to allocate DMA memory\n");
        return 0;
    }

    r69_out8(R69_REG_CFG9346, R69_CFG9346_UNLOCK);

    uint16_t cpcmd = r69_in16(R69_REG_CPLUSCMD);
    if (rtl8169.old_8169)
    {
        if (!rtl8169_dma_below_4g())
        {
            kprintf("rtl8169: DMA buffers above 4 GiB, unsupported on this chip\n");
            r69_out8(R69_REG_CFG9346, R69_CFG9346_LOCK);
            return 0;
        }
        cpcmd &= ~R69_CPCMD_PCIDAC;
    }
    r69_out16(R69_REG_CPLUSCMD, cpcmd);

    r69_out16(R69_REG_RXMAXSIZE, R69_BUF_SIZE);
    r69_out8(R69_REG_MAXTXPKT, 0x3B);

    if (!rtl8169.old_8169)
        r69_out32(R69_REG_MISC, r69_in32(R69_REG_MISC) & ~R69_MISC_RXDV_GATE);

    uint64_t tx_phys = virt_to_phys(rtl8169.tx_ring);
    uint64_t rx_phys = virt_to_phys(rtl8169.rx_ring);
    r69_out32(R69_REG_TNPDS_HI, (uint32_t)(tx_phys >> 32));
    r69_out32(R69_REG_TNPDS_LO, (uint32_t)tx_phys);
    r69_out32(R69_REG_RDSAR_HI, (uint32_t)(rx_phys >> 32));
    r69_out32(R69_REG_RDSAR_LO, (uint32_t)rx_phys);

    r69_out8(R69_REG_CFG9346, R69_CFG9346_LOCK);
    (void)r69_in8(R69_REG_CMD);

    r69_out8(R69_REG_CMD, R69_CMD_TX_ENABLE | R69_CMD_RX_ENABLE);

    uint32_t rxcfg = R69_RX_DMA_BURST;
    if (rtl8169.old_8169)
        rxcfg |= R69_RX_FIFO_THRESH;
    else if (rtl8169.g_up)
        rxcfg |= R69_RX_128_INT_EN | R69_RX_MULTI_EN | R69_RX_EARLY_OFF;
    else if (rtl8169.evl_up)
        rxcfg |= R69_RX_128_INT_EN | R69_RX_MULTI_EN;
    else
        rxcfg |= R69_RX_128_INT_EN;
    r69_out32(R69_REG_RXCONFIG, rxcfg);
    r69_out32(R69_REG_TXCONFIG, R69_TX_IFG96 | R69_TX_DMA_BURST | (rtl8169.evl_up ? R69_TX_AUTO_FIFO : 0));

    r69_out32(R69_REG_MAR0 + 4, 0xFFFFFFFF);
    r69_out32(R69_REG_MAR0, 0xFFFFFFFF);
    rxcfg = (r69_in32(R69_REG_RXCONFIG) & ~R69_RX_ACCEPT_MASK) |
            R69_RX_ACCEPT_BCAST | R69_RX_ACCEPT_MCAST | R69_RX_ACCEPT_MYPHYS;
    r69_out32(R69_REG_RXCONFIG, rxcfg);

    rtl8169_ready = true;

    uint8_t phy = 0;
    for (int ms = 0; ms < 5000; ms += 50)
    {
        phy = r69_in8(R69_REG_PHYSTATUS);
        if (phy & R69_PHY_LINK)
            break;
        task_sleep_ms(50);
    }

    if (phy & R69_PHY_LINK)
        kprintf("rtl8169: link up, %s Mbps %s duplex\n",
                (phy & R69_PHY_1000M) ? "1000" : (phy & R69_PHY_100M) ? "100" : "10",
                (phy & R69_PHY_FULLDUP) ? "full" : "half");
    else
        kprintf("rtl8169: no link yet (cable unplugged?)\n");

    return 1;
}

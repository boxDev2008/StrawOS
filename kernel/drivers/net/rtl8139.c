#define RTL_REG_MAC0        0x00
#define RTL_REG_MAR0        0x08
#define RTL_REG_TSD0        0x10
#define RTL_REG_TSAD0       0x20
#define RTL_REG_RBSTART     0x30
#define RTL_REG_CMD         0x37
#define RTL_REG_CAPR        0x38
#define RTL_REG_IMR         0x3C
#define RTL_REG_ISR         0x3E
#define RTL_REG_TCR         0x40
#define RTL_REG_RCR         0x44
#define RTL_REG_CONFIG1     0x52

#define RTL_CMD_RX_ENABLE   (1 << 3)
#define RTL_CMD_TX_ENABLE   (1 << 2)
#define RTL_CMD_RESET       (1 << 4)

#define RTL_INT_ROK   (1 << 0)
#define RTL_INT_RER   (1 << 1)
#define RTL_INT_TOK   (1 << 2)
#define RTL_INT_TER   (1 << 3)
#define RTL_INT_RXOVW (1 << 4)
#define RTL_INT_FOVW  (1 << 6)

#define RTL_RCR_AAP    (1 << 0)
#define RTL_RCR_APM    (1 << 1)
#define RTL_RCR_AM     (1 << 2)
#define RTL_RCR_AB     (1 << 3)
#define RTL_RCR_WRAP   (1 << 7)
#define RTL_RCR_RBLEN_32K (2 << 11)

#define RTL_TCR_IFG96  ((uint32_t)0x3 << 24)

#define RTL_RX_OK      (1 << 0)

#define RTL_NUM_TX_SLOTS 4
#define RTL_TX_BUF_SIZE  1536

#define RTL_RX_RING     32768
#define RTL_RX_BUF_SIZE (RTL_RX_RING + 16 + 1536)

typedef struct
{
    uint16_t io_base;
    uint8_t  *mmio_base;
    int       use_mmio;

    uint8_t  mac[6];

    uint8_t  *rx_buf;
    uint32_t  rx_offset;

    uint8_t  *tx_buf[RTL_NUM_TX_SLOTS];
    int       tx_next_slot;

    Rtl8139RxCallback rx_callback;
}
Rtl8139Device;

static Rtl8139Device rtl8139;

static uint8_t rtl_in8(uint16_t reg)
{
    if (rtl8139.use_mmio)
        return *(volatile uint8_t *)(rtl8139.mmio_base + reg);
    return inb(rtl8139.io_base + reg);
}

static uint16_t rtl_in16(uint16_t reg)
{
    if (rtl8139.use_mmio)
        return *(volatile uint16_t *)(rtl8139.mmio_base + reg);
    return inw(rtl8139.io_base + reg);
}

static uint32_t rtl_in32(uint16_t reg)
{
    if (rtl8139.use_mmio)
        return *(volatile uint32_t *)(rtl8139.mmio_base + reg);
    return inl(rtl8139.io_base + reg);
}

static void rtl_out8(uint16_t reg, uint8_t val)
{
    if (rtl8139.use_mmio)
        *(volatile uint8_t *)(rtl8139.mmio_base + reg) = val;
    else
        outb(rtl8139.io_base + reg, val);
}

static void rtl_out16(uint16_t reg, uint16_t val)
{
    if (rtl8139.use_mmio)
        *(volatile uint16_t *)(rtl8139.mmio_base + reg) = val;
    else
        outw(rtl8139.io_base + reg, val);
}

static void rtl_out32(uint16_t reg, uint32_t val)
{
    if (rtl8139.use_mmio)
        *(volatile uint32_t *)(rtl8139.mmio_base + reg) = val;
    else
        outl(rtl8139.io_base + reg, val);
}

static bool rtl8139_ready;

void rtl8139_poll(void)
{
    if (!rtl8139_ready)
        return;
    uint16_t status = rtl_in16(RTL_REG_ISR);
    if (status)
        rtl_out16(RTL_REG_ISR, status);

    {
        while (!(rtl_in8(RTL_REG_CMD) & 0x01))
        {
            uint8_t *hdr = rtl8139.rx_buf + rtl8139.rx_offset;

            uint16_t rx_status = *(uint16_t *)(hdr + 0);
            uint16_t rx_len    = *(uint16_t *)(hdr + 2);

            if (rx_len == 0xFFF0)
                break;
            if (rx_status & RTL_RX_OK && rx_len >= 4 && rx_len <= 1518 + 4)
            {
                uint8_t *frame = hdr + 4;
                uint16_t frame_len = rx_len - 4;

                if (rtl8139.rx_callback)
                    rtl8139.rx_callback(frame, frame_len);
            }

            rtl8139.rx_offset = (rtl8139.rx_offset + rx_len + 4 + 3) & ~3u;
            rtl8139.rx_offset %= RTL_RX_RING;

            rtl_out16(RTL_REG_CAPR, (uint16_t)(rtl8139.rx_offset - 16));
        }
    }
}

static void rtl8139_irq_handler(void)
{
    rtl8139_poll();
}

void rtl8139_set_rx_callback(Rtl8139RxCallback cb)
{
    rtl8139.rx_callback = cb;
}

const uint8_t *rtl8139_mac(void)
{
    return rtl8139.mac;
}

void rtl8139_send(const uint8_t *frame, uint16_t len)
{
    if (len > RTL_TX_BUF_SIZE)
        len = RTL_TX_BUF_SIZE;

    int slot = rtl8139.tx_next_slot;
    rtl8139.tx_next_slot = (rtl8139.tx_next_slot + 1) % RTL_NUM_TX_SLOTS;

    while (!(rtl_in32(RTL_REG_TSD0 + slot * 4) & ((1 << 13) | (1 << 15))))
        pause();

    uint8_t *buf = rtl8139.tx_buf[slot];
    memcpy(buf, frame, len);

    if (len < 60)
    {
        memset(buf + len, 0, 60 - len);
        len = 60;
    }

    rtl_out32(RTL_REG_TSAD0 + slot * 4, (uint32_t)virt_to_phys(buf));
    rtl_out32(RTL_REG_TSD0 + slot * 4, (uint32_t)len);
}

static void rtl8139_read_mac(void)
{
    for (int i = 0; i < 6; i++)
        rtl8139.mac[i] = rtl_in8(RTL_REG_MAC0 + i);
}

int rtl8139_init(void)
{
    PciDevice dev;
    if (!pci_find_device(RTL8139_VENDOR_ID, RTL8139_DEVICE_ID, &dev))
    {
        kprintf("rtl8139: no card found on PCI bus\n");
        return 0;
    }

    pci_enable_device(&dev);

    memset(&rtl8139, 0, sizeof(rtl8139));

    if (!pci_bar_is_io(dev.bar[0]))
    {
        kprintf("rtl8139: BAR0 is not an I/O BAR, using MMIO BAR1\n");
        rtl8139.use_mmio = 1;
        rtl8139.mmio_base = (uint8_t *)vmm_map_mmio(pci_bar_addr(&dev, 1), 0x100);
    }
    else
    {
        rtl8139.use_mmio = 0;
        rtl8139.io_base = (uint16_t)pci_bar_addr(&dev, 0);
    }

    rtl_out8(RTL_REG_CONFIG1, 0x00);

    rtl_out8(RTL_REG_CMD, RTL_CMD_RESET);
    {
        int spins = 0;
        while (rtl_in8(RTL_REG_CMD) & RTL_CMD_RESET)
        {
            pause();
            if (++spins > 1000000)
            {
                kprintf("rtl8139: reset timed out\n");
                return 0;
            }
        }
    }

    rtl8139_read_mac();
    kprintf("rtl8139: found card, MAC %x:%x:%x:%x:%x:%x\n",
            rtl8139.mac[0], rtl8139.mac[1], rtl8139.mac[2],
            rtl8139.mac[3], rtl8139.mac[4], rtl8139.mac[5]);

    uint64_t rx_pages = DIV_CEIL(RTL_RX_BUF_SIZE, PAGE_SIZE);
    rtl8139.rx_buf = (uint8_t *)pmm_alloc_pages(rx_pages);
    if (!rtl8139.rx_buf)
    {
        kprintf("rtl8139: failed to allocate RX buffer\n");
        return 0;
    }
    rtl8139.rx_buf = (uint8_t *)phys_to_virt(virt_to_phys(rtl8139.rx_buf));
    rtl8139.rx_offset = 0;
    rtl_out32(RTL_REG_RBSTART, (uint32_t)virt_to_phys(rtl8139.rx_buf));

    for (int i = 0; i < RTL_NUM_TX_SLOTS; i++)
    {
        rtl8139.tx_buf[i] = (uint8_t *)pmm_alloc_page();
        if (!rtl8139.tx_buf[i])
        {
            kprintf("rtl8139: failed to allocate TX buffer %d\n", i);
            return 0;
        }
        rtl8139.tx_buf[i] = (uint8_t *)phys_to_virt(virt_to_phys(rtl8139.tx_buf[i]));
    }
    rtl8139.tx_next_slot = 0;

    rtl_out32(RTL_REG_RCR, RTL_RCR_AAP | RTL_RCR_APM | RTL_RCR_AM | RTL_RCR_AB | RTL_RCR_WRAP | RTL_RCR_RBLEN_32K);
    rtl_out32(RTL_REG_TCR, RTL_TCR_IFG96);

    rtl_out8(RTL_REG_CMD, RTL_CMD_RX_ENABLE | RTL_CMD_TX_ENABLE);

    rtl_out16(RTL_REG_IMR, RTL_INT_ROK | RTL_INT_RER | RTL_INT_TOK | RTL_INT_TER | RTL_INT_RXOVW | RTL_INT_FOVW);

    rtl8139_ready = true;
    idt_set_irq_handler(IRQ_NIC, rtl8139_irq_handler);
    ioapic_set_irq(dev.irq_line, lapic_id(), IRQ_NIC);

    kprintf("rtl8139: initialized (io_base=%x irq_line=%d)\n",
            rtl8139.use_mmio ? 0 : rtl8139.io_base, dev.irq_line);

    return 1;
}
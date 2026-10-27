#define XHCI_CAP_CAPLENGTH  0x00
#define XHCI_CAP_HCSPARAMS1 0x04
#define XHCI_CAP_HCSPARAMS2 0x08
#define XHCI_CAP_HCCPARAMS1 0x10
#define XHCI_CAP_DBOFF      0x14
#define XHCI_CAP_RTSOFF     0x18

#define XHCI_HCC_AC64       (1 << 0)
#define XHCI_HCC_CSZ        (1 << 2)

#define XHCI_OP_USBCMD      0x00
#define XHCI_OP_USBSTS      0x04
#define XHCI_OP_DNCTRL      0x14
#define XHCI_OP_CRCR        0x18
#define XHCI_OP_DCBAAP      0x30
#define XHCI_OP_CONFIG      0x38
#define XHCI_OP_PORTSC(p)   (0x400 + ((p) - 1) * 0x10)

#define XHCI_CMD_RUN        (1 << 0)
#define XHCI_CMD_HCRST      (1 << 1)

#define XHCI_STS_HCH        (1 << 0)
#define XHCI_STS_CNR        (1 << 11)

#define XHCI_PORTSC_CCS     (1 << 0)
#define XHCI_PORTSC_PED     (1 << 1)
#define XHCI_PORTSC_PR      (1 << 4)
#define XHCI_PORTSC_PP      (1 << 9)
#define XHCI_PORTSC_PRC     (1 << 21)
#define XHCI_PORTSC_CHANGE  (0x7Fu << 17)
#define XHCI_PORTSC_RW1C    (XHCI_PORTSC_PED | XHCI_PORTSC_CHANGE)

#define XHCI_RT_IMAN        0x20
#define XHCI_RT_ERSTSZ      0x28
#define XHCI_RT_ERSTBA      0x30
#define XHCI_RT_ERDP        0x38

#define XHCI_ERDP_EHB       (1 << 3)

#define XHCI_XCAP_LEGACY    1
#define XHCI_LEGACY_BIOS_OWNED (1 << 16)
#define XHCI_LEGACY_OS_OWNED   (1 << 24)

#define TRB_CYCLE           (1 << 0)
#define TRB_TOGGLE_CYCLE    (1 << 1)
#define TRB_IOC             (1 << 5)
#define TRB_IDT             (1 << 6)
#define TRB_DIR_IN          (1 << 16)
#define TRB_TYPE(t)         ((uint32_t)(t) << 10)
#define TRB_GET_TYPE(c)     (((c) >> 10) & 0x3F)
#define TRB_SLOT(s)         ((uint32_t)(s) << 24)
#define TRB_EP(e)           ((uint32_t)(e) << 16)

#define TRB_NORMAL          1
#define TRB_SETUP           2
#define TRB_DATA            3
#define TRB_STATUS          4
#define TRB_LINK            6
#define TRB_ENABLE_SLOT     9
#define TRB_ADDRESS_DEVICE  11
#define TRB_CONFIGURE_EP    12
#define TRB_EVALUATE_CTX    13
#define TRB_RESET_EP        14
#define TRB_SET_TR_DEQUEUE  16
#define TRB_EV_TRANSFER     32
#define TRB_EV_CMD_COMPLETE 33

#define XHCI_CC_SUCCESS     1
#define XHCI_CC_STALL       6
#define XHCI_CC_SHORT       13
#define XHCI_CC_TIMEOUT     0xFF

#define XHCI_SPEED_FULL     1
#define XHCI_SPEED_LOW      2
#define XHCI_SPEED_HIGH     3
#define XHCI_SPEED_SUPER    4

#define XHCI_EP_TYPE_CONTROL 4
#define XHCI_EP_TYPE_INT_IN  7

#define USB_REQ_SET_CONFIGURATION 9
#define USB_REQ_GET_DESCRIPTOR    6
#define USB_HID_REQ_SET_IDLE      0x0A
#define USB_HID_REQ_SET_PROTOCOL  0x0B

#define USB_HID_PROTOCOL_BOOT     0
#define USB_HID_PROTOCOL_REPORT   1

#define USB_DESC_DEVICE    1
#define USB_DESC_CONFIG    2
#define USB_DESC_INTERFACE 4
#define USB_DESC_ENDPOINT  5
#define USB_DESC_HID       0x21
#define USB_DESC_REPORT    0x22

#define USB_CLASS_HID      3
#define USB_SUBCLASS_BOOT  1
#define USB_CLASS_HUB      9

#define USB_REQ_GET_STATUS        0
#define USB_REQ_CLEAR_FEATURE     1
#define USB_REQ_SET_FEATURE       3
#define USB_HUB_REQ_SET_DEPTH     12

#define USB_DESC_HUB       0x29
#define USB_DESC_SS_HUB    0x2A

#define USB_HUB_PORT_RESET        4
#define USB_HUB_PORT_POWER        8
#define USB_HUB_C_PORT_CONNECTION 16
#define USB_HUB_C_PORT_RESET      20

#define USB_HUB_STATUS_CONNECTION (1 << 0)
#define USB_HUB_STATUS_ENABLE     (1 << 1)
#define USB_HUB_STATUS_RESET      (1 << 4)
#define USB_HUB_STATUS_LOW_SPEED  (1 << 9)
#define USB_HUB_STATUS_HIGH_SPEED (1 << 10)
#define USB_HUB_CHANGE_RESET      (1 << 4)

#define XHCI_SLOT_HUB             (1u << 26)

static XhciController xhci_controllers[XHCI_MAX_CONTROLLERS];
static int xhci_controller_count = 0;
static volatile int xhci_ready = 0;

static inline uint32_t xhci_read32(volatile uint8_t *base, uint32_t off)
{
    return *(volatile uint32_t *)(base + off);
}

static inline void xhci_write32(volatile uint8_t *base, uint32_t off, uint32_t val)
{
    *(volatile uint32_t *)(base + off) = val;
}

static inline void xhci_write64(volatile uint8_t *base, uint32_t off, uint64_t val)
{
    xhci_write32(base, off, (uint32_t)val);
    xhci_write32(base, off + 4, (uint32_t)(val >> 32));
}

static void xhci_delay_ms(uint32_t ms)
{
    while (ms--)
        pit_sleep_ms(1);
}

static int xhci_wait_bits(volatile uint8_t *base, uint32_t off, uint32_t mask, uint32_t want, uint32_t timeout_ms)
{
    for (uint32_t i = 0; i <= timeout_ms; i++)
    {
        if ((xhci_read32(base, off) & mask) == want)
            return 1;
        xhci_delay_ms(1);
    }
    return 0;
}

static void *xhci_alloc_dma(uint64_t *phys)
{
    void *page = pmm_alloc_page();
    if (!page)
    {
        kprintf("xhci: out of memory\n");
        return NULL;
    }
    memset(page, 0, PAGE_SIZE);
    *phys = virt_to_phys(page);
    return page;
}

static int xhci_ring_init(XhciRing *ring)
{
    ring->trbs = (volatile XhciTrb *)xhci_alloc_dma(&ring->phys);
    ring->enqueue = 0;
    ring->cycle = 1;
    return ring->trbs != NULL;
}

static uint64_t xhci_ring_push(XhciRing *ring, uint64_t param, uint32_t status, uint32_t control)
{
    volatile XhciTrb *trb = &ring->trbs[ring->enqueue];
    uint64_t trb_phys = ring->phys + ring->enqueue * sizeof(XhciTrb);

    trb->param = param;
    trb->status = status;
    __asm__ volatile("" ::: "memory");
    trb->control = (control & ~TRB_CYCLE) | ring->cycle;

    if (++ring->enqueue == XHCI_RING_TRBS - 1)
    {
        volatile XhciTrb *link = &ring->trbs[ring->enqueue];
        link->param = ring->phys;
        link->status = 0;
        __asm__ volatile("" ::: "memory");
        link->control = TRB_TYPE(TRB_LINK) | TRB_TOGGLE_CYCLE | ring->cycle;

        ring->enqueue = 0;
        ring->cycle ^= 1;
    }

    return trb_phys;
}

static inline void xhci_ring_doorbell(XhciController *hc, uint32_t slot, uint32_t target)
{
    __asm__ volatile("" ::: "memory");
    hc->db[slot] = target;
}

static inline uint8_t *xhci_ctx(XhciController *hc, uint8_t *base, uint32_t index)
{
    return base + index * hc->ctx_size;
}

static inline uint32_t *xhci_input_ctx(XhciController *hc, XhciDevice *dev, uint32_t index)
{
    return (uint32_t *)xhci_ctx(hc, dev->in_ctx, index);
}

static int xhci_next_event(XhciController *hc, XhciTrb *out)
{
    volatile XhciTrb *trb = &hc->events[hc->ev_dequeue];
    if ((trb->control & TRB_CYCLE) != hc->ev_cycle)
        return 0;

    out->param = trb->param;
    out->status = trb->status;
    out->control = trb->control;

    if (++hc->ev_dequeue == XHCI_RING_TRBS)
    {
        hc->ev_dequeue = 0;
        hc->ev_cycle ^= 1;
    }

    xhci_write64(hc->rt, XHCI_RT_ERDP,
                 (hc->events_phys + hc->ev_dequeue * sizeof(XhciTrb)) | XHCI_ERDP_EHB);
    return 1;
}

static void xhci_queue_interrupt(XhciController *hc, XhciDevice *dev, XhciHidEndpoint *ep)
{
    xhci_ring_push(&ep->ring, ep->buf_phys, ep->max_packet, TRB_TYPE(TRB_NORMAL) | TRB_IOC);
    xhci_ring_doorbell(hc, dev->slot_id, ep->dci);
}

static void xhci_handle_transfer(XhciController *hc, const XhciTrb *ev)
{
    uint8_t slot = (uint8_t)(ev->control >> 24);
    uint8_t dci  = (uint8_t)((ev->control >> 16) & 0x1F);
    uint8_t cc   = (uint8_t)(ev->status >> 24);

    if (slot == 0 || slot > hc->max_slots || !hc->slots[slot])
        return;

    XhciDevice *dev = hc->slots[slot];
    for (int i = 0; i < dev->num_hid; i++)
    {
        XhciHidEndpoint *ep = &dev->hid[i];
        if (ep->dci != dci)
            continue;

        if (cc != XHCI_CC_SUCCESS && cc != XHCI_CC_SHORT)
        {
            kprintf("xhci: slot %d ep %d transfer error %d, endpoint stopped\n", slot, dci, cc);
            return;
        }

        uint32_t residue = ev->status & 0xFFFFFF;
        size_t len = residue < ep->max_packet ? ep->max_packet - residue : 0;

        if (ep->protocol == USB_HID_PROTO_KEYBOARD)
            usb_hid_keyboard_report(&ep->kbd, ep->buf, len);
        else if (ep->use_layout)
            usb_hid_mouse_report_layout(&ep->layout, ep->buf, len);
        else
            usb_hid_mouse_report(ep->buf, len);

        xhci_queue_interrupt(hc, dev, ep);
        return;
    }
}

static int xhci_wait_event(XhciController *hc, uint32_t type, uint8_t slot, XhciTrb *out, uint32_t timeout_ms)
{
    for (uint32_t i = 0; i <= timeout_ms; i++)
    {
        XhciTrb ev;
        while (xhci_next_event(hc, &ev))
        {
            uint32_t ev_type = TRB_GET_TYPE(ev.control);

            if (ev_type == type &&
                (type != TRB_EV_TRANSFER ||
                 ((ev.control >> 24) == slot && ((ev.control >> 16) & 0x1F) == 1)))
            {
                *out = ev;
                return 1;
            }

            if (ev_type == TRB_EV_TRANSFER)
                xhci_handle_transfer(hc, &ev);
        }
        xhci_delay_ms(1);
    }
    return 0;
}

static int xhci_command(XhciController *hc, uint64_t param, uint32_t control, XhciTrb *out)
{
    xhci_ring_push(&hc->cmd, param, 0, control);
    xhci_ring_doorbell(hc, 0, 0);

    XhciTrb ev;
    if (!xhci_wait_event(hc, TRB_EV_CMD_COMPLETE, 0, &ev, 1000))
    {
        kprintf("xhci: command %d timed out\n", (int)TRB_GET_TYPE(control));
        return 0;
    }

    if (out)
        *out = ev;

    uint8_t cc = (uint8_t)(ev.status >> 24);
    if (cc != XHCI_CC_SUCCESS)
    {
        kprintf("xhci: command %d failed, completion code %d\n", (int)TRB_GET_TYPE(control), cc);
        return 0;
    }
    return 1;
}

static void xhci_recover_ep0(XhciController *hc, XhciDevice *dev)
{
    xhci_command(hc, 0, TRB_TYPE(TRB_RESET_EP) | TRB_SLOT(dev->slot_id) | TRB_EP(1), NULL);

    uint64_t deq = (dev->ep0.phys + dev->ep0.enqueue * sizeof(XhciTrb)) | dev->ep0.cycle;
    xhci_command(hc, deq, TRB_TYPE(TRB_SET_TR_DEQUEUE) | TRB_SLOT(dev->slot_id) | TRB_EP(1), NULL);
}

static int xhci_control(XhciController *hc, XhciDevice *dev,
                        uint8_t request_type, uint8_t request,
                        uint16_t value, uint16_t index,
                        uint16_t length, void *data)
{
    int in = (request_type & 0x80) != 0;

    if (length > PAGE_SIZE)
        return 0;

    uint64_t setup = (uint64_t)request_type |
                     ((uint64_t)request << 8) |
                     ((uint64_t)value << 16) |
                     ((uint64_t)index << 32) |
                     ((uint64_t)length << 48);

    uint32_t trt = length == 0 ? 0 : (in ? 3 : 2);
    xhci_ring_push(&dev->ep0, setup, 8, TRB_TYPE(TRB_SETUP) | TRB_IDT | (trt << 16));

    if (length)
    {
        if (!in)
            memcpy(hc->dma_buf, data, length);
        xhci_ring_push(&dev->ep0, hc->dma_buf_phys, length,
                       TRB_TYPE(TRB_DATA) | (in ? TRB_DIR_IN : 0));
    }

    uint32_t status_dir = (length && in) ? 0 : TRB_DIR_IN;
    xhci_ring_push(&dev->ep0, 0, 0, TRB_TYPE(TRB_STATUS) | TRB_IOC | status_dir);

    xhci_ring_doorbell(hc, dev->slot_id, 1);

    XhciTrb ev;
    if (!xhci_wait_event(hc, TRB_EV_TRANSFER, dev->slot_id, &ev, 1000))
    {
        kprintf("xhci: control transfer timed out (req %d)\n", request);
        return 0;
    }

    uint8_t cc = (uint8_t)(ev.status >> 24);
    if (cc != XHCI_CC_SUCCESS && cc != XHCI_CC_SHORT)
    {
        if (cc == XHCI_CC_STALL)
            xhci_recover_ep0(hc, dev);
        return 0;
    }

    if (length && in)
        memcpy(data, hc->dma_buf, length);
    return 1;
}

static uint16_t xhci_default_mps0(uint8_t speed)
{
    switch (speed)
    {
        case XHCI_SPEED_LOW:
        case XHCI_SPEED_FULL:  return 8;
        case XHCI_SPEED_HIGH:  return 64;
        default:               return 512;
    }
}

static uint8_t xhci_ep_interval(uint8_t speed, uint8_t b_interval)
{
    if (speed == XHCI_SPEED_HIGH || speed >= XHCI_SPEED_SUPER)
    {
        if (b_interval < 1)  b_interval = 1;
        if (b_interval > 16) b_interval = 16;
        return b_interval - 1;
    }

    uint32_t microframes = (uint32_t)(b_interval ? b_interval : 1) * 8;
    uint8_t exp = 0;
    while ((1u << (exp + 1)) <= microframes)
        exp++;
    return (uint8_t)MAX(3, MIN(exp, 10));
}

static void xhci_parse_config(XhciDevice *dev, const uint8_t *cfg, uint16_t total)
{
    int      cur_proto = 0;
    uint8_t  cur_iface = 0;
    uint8_t  cur_boot = 0;
    uint16_t cur_report_len = 0;

    for (uint16_t off = 0; off + 2 <= total; )
    {
        uint8_t len  = cfg[off];
        uint8_t type = cfg[off + 1];
        if (len < 2 || off + len > total)
            break;

        if (type == USB_DESC_INTERFACE && len >= 9)
        {
            cur_proto = 0;
            cur_report_len = 0;
            if (cfg[off + 3] == 0 && cfg[off + 5] == USB_CLASS_HID)
            {
                uint8_t subclass = cfg[off + 6];
                uint8_t proto    = cfg[off + 7];

                cur_iface = cfg[off + 2];
                cur_boot  = subclass == USB_SUBCLASS_BOOT &&
                            (proto == USB_HID_PROTO_KEYBOARD || proto == USB_HID_PROTO_MOUSE);
                if (cur_boot)
                    cur_proto = proto;
                else if (proto != USB_HID_PROTO_KEYBOARD)
                    cur_proto = USB_HID_PROTO_MOUSE;
            }
        }
        else if (type == USB_DESC_HID && len >= 9 && cur_proto)
        {
            if (cfg[off + 6] == USB_DESC_REPORT)
                cur_report_len = (uint16_t)(cfg[off + 7] | (cfg[off + 8] << 8));
        }
        else if (type == USB_DESC_ENDPOINT && len >= 7 && cur_proto)
        {
            uint8_t addr  = cfg[off + 2];
            uint8_t attrs = cfg[off + 3];

            if ((addr & 0x80) && (attrs & 0x3) == 3 && dev->num_hid < XHCI_MAX_HID_EPS)
            {
                XhciHidEndpoint *ep = &dev->hid[dev->num_hid++];
                memset(ep, 0, sizeof(*ep));
                ep->dci        = (uint8_t)((addr & 0x0F) * 2 + 1);
                ep->iface      = cur_iface;
                ep->protocol   = (uint8_t)cur_proto;
                ep->boot       = cur_boot;
                ep->report_desc_len = cur_report_len;
                ep->max_packet = (uint16_t)((cfg[off + 4] | (cfg[off + 5] << 8)) & 0x7FF);
                ep->interval   = xhci_ep_interval(dev->speed, cfg[off + 6]);
                if (ep->max_packet == 0 || ep->max_packet > 64)
                    ep->max_packet = 64;

                cur_proto = 0;
            }
        }

        off += len;
    }
}

static void xhci_setup_hid_mouse(XhciController *hc, XhciDevice *dev, XhciHidEndpoint *ep)
{
    uint16_t len = (uint16_t)MIN(ep->report_desc_len ? ep->report_desc_len : 256, PAGE_SIZE);
    uint8_t *desc = (uint8_t *)kmalloc(len);

    if (desc && xhci_control(hc, dev, 0x81, USB_REQ_GET_DESCRIPTOR, USB_DESC_REPORT << 8,
                             ep->iface, len, desc) &&
        usb_hid_parse_mouse_layout(desc, len, &ep->layout))
    {
        ep->use_layout = 1;
        if (ep->boot)
            xhci_control(hc, dev, 0x21, USB_HID_REQ_SET_PROTOCOL, USB_HID_PROTOCOL_REPORT, ep->iface, 0, NULL);
    }
    else if (ep->boot)
    {
        xhci_control(hc, dev, 0x21, USB_HID_REQ_SET_PROTOCOL, USB_HID_PROTOCOL_BOOT, ep->iface, 0, NULL);
    }

    if (desc)
        kfree(desc);
}

static int xhci_configure_hid_endpoints(XhciController *hc, XhciDevice *dev)
{
    uint8_t *bufs = (uint8_t *)pmm_alloc_page();
    if (!bufs)
        return 0;
    memset(bufs, 0, PAGE_SIZE);

    uint32_t *icc = xhci_input_ctx(hc, dev, 0);
    icc[0] = 0;
    icc[1] = 1;

    uint8_t max_dci = 1;

    for (int i = 0; i < dev->num_hid; i++)
    {
        XhciHidEndpoint *ep = &dev->hid[i];
        if (!xhci_ring_init(&ep->ring))
            return 0;

        ep->buf = bufs + i * 64;
        ep->buf_phys = virt_to_phys(ep->buf);

        uint32_t *epc = xhci_input_ctx(hc, dev, 1 + ep->dci);
        memset(epc, 0, hc->ctx_size);
        epc[0] = (uint32_t)ep->interval << 16;
        epc[1] = (3 << 1) | (XHCI_EP_TYPE_INT_IN << 3) | ((uint32_t)ep->max_packet << 16);
        epc[2] = (uint32_t)ep->ring.phys | 1;
        epc[3] = (uint32_t)(ep->ring.phys >> 32);
        epc[4] = ep->max_packet | ((uint32_t)ep->max_packet << 16);

        icc[1] |= 1u << ep->dci;
        if (ep->dci > max_dci)
            max_dci = ep->dci;
    }

    uint32_t *slot = xhci_input_ctx(hc, dev, 1);
    slot[0] = (slot[0] & ~(0x1Fu << 27)) | ((uint32_t)max_dci << 27);

    return xhci_command(hc, dev->in_ctx_phys,
                        TRB_TYPE(TRB_CONFIGURE_EP) | TRB_SLOT(dev->slot_id), NULL);
}

static void xhci_setup_device(XhciController *hc, XhciDevice *parent, uint8_t port, uint8_t speed);

static void xhci_append_port(char *path, size_t size, uint8_t port)
{
    char digits[4];
    int n = 0;
    do
    {
        digits[n++] = (char)('0' + port % 10);
        port /= 10;
    } while (port);

    size_t len = strlen(path);
    if (len && len + 1 < size)
        path[len++] = '.';
    while (n && len + 1 < size)
        path[len++] = digits[--n];
    path[len] = '\0';
}

static int xhci_hub_port_status(XhciController *hc, XhciDevice *hub, uint8_t port,
                                uint16_t *status, uint16_t *change)
{
    uint8_t st[4];
    if (!xhci_control(hc, hub, 0xA3, USB_REQ_GET_STATUS, 0, port, 4, st))
        return 0;
    *status = (uint16_t)(st[0] | (st[1] << 8));
    *change = (uint16_t)(st[2] | (st[3] << 8));
    return 1;
}

static void xhci_probe_hub_port(XhciController *hc, XhciDevice *hub, uint8_t port)
{
    uint16_t status, change;
    if (!xhci_hub_port_status(hc, hub, port, &status, &change) ||
        !(status & USB_HUB_STATUS_CONNECTION))
        return;

    xhci_control(hc, hub, 0x23, USB_REQ_SET_FEATURE, USB_HUB_PORT_RESET, port, 0, NULL);

    int reset_done = 0;
    for (int i = 0; i < 500 && !reset_done; i++)
    {
        xhci_delay_ms(1);
        if (!xhci_hub_port_status(hc, hub, port, &status, &change))
            return;
        reset_done = (change & USB_HUB_CHANGE_RESET) && !(status & USB_HUB_STATUS_RESET);
    }

    xhci_control(hc, hub, 0x23, USB_REQ_CLEAR_FEATURE, USB_HUB_C_PORT_RESET, port, 0, NULL);
    xhci_control(hc, hub, 0x23, USB_REQ_CLEAR_FEATURE, USB_HUB_C_PORT_CONNECTION, port, 0, NULL);

    if (!reset_done || !(status & USB_HUB_STATUS_ENABLE))
    {
        kprintf("xhci: hub %s port %d: reset failed\n", hub->path, port);
        return;
    }

    xhci_delay_ms(10);

    uint8_t speed;
    if (hub->speed >= XHCI_SPEED_SUPER)
        speed = hub->speed;
    else if (status & USB_HUB_STATUS_LOW_SPEED)
        speed = XHCI_SPEED_LOW;
    else if (status & USB_HUB_STATUS_HIGH_SPEED)
        speed = XHCI_SPEED_HIGH;
    else
        speed = XHCI_SPEED_FULL;

    xhci_setup_device(hc, hub, port, speed);
}

static void xhci_setup_hub(XhciController *hc, XhciDevice *hub)
{
    int super = hub->speed >= XHCI_SPEED_SUPER;

    if (super && !xhci_control(hc, hub, 0x20, USB_HUB_REQ_SET_DEPTH, hub->tier, 0, 0, NULL))
    {
        kprintf("xhci: hub %s: SET_HUB_DEPTH failed\n", hub->path);
        return;
    }

    uint8_t desc[12];
    memset(desc, 0, sizeof(desc));
    uint8_t type = super ? USB_DESC_SS_HUB : USB_DESC_HUB;
    if (!xhci_control(hc, hub, 0xA0, USB_REQ_GET_DESCRIPTOR, (uint16_t)(type << 8), 0, sizeof(desc), desc))
    {
        kprintf("xhci: hub %s: failed to read hub descriptor\n", hub->path);
        return;
    }

    uint8_t  nports  = desc[2];
    uint16_t chars   = (uint16_t)(desc[3] | (desc[4] << 8));
    uint32_t power_ms = (uint32_t)desc[5] * 2;

    uint32_t *icc  = xhci_input_ctx(hc, hub, 0);
    uint32_t *slot = xhci_input_ctx(hc, hub, 1);
    icc[0] = 0;
    icc[1] = 1;
    slot[0] |= XHCI_SLOT_HUB;
    slot[1] = (slot[1] & 0x00FFFFFFu) | ((uint32_t)nports << 24);
    if (hub->speed == XHCI_SPEED_HIGH)
        slot[2] = (slot[2] & ~(0x3u << 16)) | ((uint32_t)((chars >> 5) & 0x3) << 16);

    if (!xhci_command(hc, hub->in_ctx_phys, TRB_TYPE(TRB_CONFIGURE_EP) | TRB_SLOT(hub->slot_id), NULL))
    {
        kprintf("xhci: hub %s: configure failed\n", hub->path);
        return;
    }

    kprintf("xhci: hub %s: %s hub, %d ports\n", hub->path, super ? "USB3" : "USB2", nports);

    for (uint8_t p = 1; p <= nports; p++)
        xhci_control(hc, hub, 0x23, USB_REQ_SET_FEATURE, USB_HUB_PORT_POWER, p, 0, NULL);

    xhci_delay_ms(MIN(power_ms, 500) + 100);

    for (uint8_t p = 1; p <= nports; p++)
        xhci_probe_hub_port(hc, hub, p);
}

static void xhci_setup_device(XhciController *hc, XhciDevice *parent, uint8_t port, uint8_t speed)
{
    uint8_t tier = parent ? (uint8_t)(parent->tier + 1) : 0;
    if (tier >= XHCI_MAX_HUB_TIER)
    {
        kprintf("xhci: hub %s port %d: too many hub tiers, ignoring\n", parent->path, port);
        return;
    }

    XhciTrb ev;
    if (!xhci_command(hc, 0, TRB_TYPE(TRB_ENABLE_SLOT), &ev))
        return;

    uint8_t slot_id = (uint8_t)(ev.control >> 24);
    if (slot_id == 0 || slot_id > hc->max_slots)
        return;

    XhciDevice *dev = (XhciDevice *)kcalloc(1, sizeof(XhciDevice));
    if (!dev)
        return;

    dev->slot_id = slot_id;
    dev->speed = speed;
    dev->tier = tier;

    if (parent)
    {
        dev->port = parent->port;
        dev->route = parent->route | ((uint32_t)MIN(port, 15) << (parent->tier * 4));
        strcpy(dev->path, parent->path);

        if (speed == XHCI_SPEED_LOW || speed == XHCI_SPEED_FULL)
        {
            if (parent->speed == XHCI_SPEED_HIGH)
            {
                dev->tt_slot = parent->slot_id;
                dev->tt_port = port;
            }
            else
            {
                dev->tt_slot = parent->tt_slot;
                dev->tt_port = parent->tt_port;
            }
        }
    }
    else
    {
        dev->port = port;
    }
    xhci_append_port(dev->path, sizeof(dev->path), port);

    uint64_t out_phys;
    dev->out_ctx = (uint8_t *)xhci_alloc_dma(&out_phys);
    dev->in_ctx = (uint8_t *)xhci_alloc_dma(&dev->in_ctx_phys);
    if (!dev->out_ctx || !dev->in_ctx || !xhci_ring_init(&dev->ep0))
        return;

    hc->dcbaa[slot_id] = out_phys;

    uint16_t mps0 = xhci_default_mps0(speed);

    uint32_t *icc  = xhci_input_ctx(hc, dev, 0);
    uint32_t *slot = xhci_input_ctx(hc, dev, 1);
    uint32_t *ep0  = xhci_input_ctx(hc, dev, 2);

    icc[1]  = 0x3;
    slot[0] = (1u << 27) | ((uint32_t)speed << 20) | (dev->route & 0xFFFFF);
    slot[1] = (uint32_t)dev->port << 16;
    slot[2] = dev->tt_slot | ((uint32_t)dev->tt_port << 8);
    ep0[1]  = (3 << 1) | (XHCI_EP_TYPE_CONTROL << 3) | ((uint32_t)mps0 << 16);
    ep0[2]  = (uint32_t)dev->ep0.phys | 1;
    ep0[3]  = (uint32_t)(dev->ep0.phys >> 32);
    ep0[4]  = 8;

    if (!xhci_command(hc, dev->in_ctx_phys, TRB_TYPE(TRB_ADDRESS_DEVICE) | TRB_SLOT(slot_id), NULL))
        return;

    hc->slots[slot_id] = dev;
    xhci_delay_ms(2);

    uint8_t desc[18];
    if (!xhci_control(hc, dev, 0x80, USB_REQ_GET_DESCRIPTOR, USB_DESC_DEVICE << 8, 0, 8, desc))
    {
        kprintf("xhci: port %s: failed to read device descriptor\n", dev->path);
        return;
    }

    if (speed < XHCI_SPEED_SUPER && desc[7] && desc[7] != mps0)
    {
        mps0 = desc[7];
        icc[0] = 0;
        icc[1] = 0x2;
        ep0[1] = (ep0[1] & 0xFFFF) | ((uint32_t)mps0 << 16);
        if (!xhci_command(hc, dev->in_ctx_phys, TRB_TYPE(TRB_EVALUATE_CTX) | TRB_SLOT(slot_id), NULL))
            return;
    }

    if (!xhci_control(hc, dev, 0x80, USB_REQ_GET_DESCRIPTOR, USB_DESC_DEVICE << 8, 0, 18, desc))
        return;

    uint8_t  dev_class = desc[4];
    uint16_t vendor  = (uint16_t)(desc[8] | (desc[9] << 8));
    uint16_t product = (uint16_t)(desc[10] | (desc[11] << 8));

    uint8_t cfg_hdr[9];
    if (!xhci_control(hc, dev, 0x80, USB_REQ_GET_DESCRIPTOR, USB_DESC_CONFIG << 8, 0, 9, cfg_hdr))
        return;

    uint16_t total = (uint16_t)MIN((uint16_t)(cfg_hdr[2] | (cfg_hdr[3] << 8)), PAGE_SIZE);
    uint8_t *cfg = (uint8_t *)kmalloc(total);
    if (!cfg)
        return;

    if (!xhci_control(hc, dev, 0x80, USB_REQ_GET_DESCRIPTOR, USB_DESC_CONFIG << 8, 0, total, cfg))
    {
        kfree(cfg);
        return;
    }

    xhci_parse_config(dev, cfg, total);
    uint8_t config_value = cfg[5];
    kfree(cfg);

    if (dev_class == USB_CLASS_HUB)
    {
        if (!xhci_control(hc, dev, 0x00, USB_REQ_SET_CONFIGURATION, config_value, 0, 0, NULL))
        {
            kprintf("xhci: port %s: hub SET_CONFIGURATION failed\n", dev->path);
            return;
        }
        xhci_setup_hub(hc, dev);
        return;
    }

    if (dev->num_hid == 0)
    {
        kprintf("xhci: port %s: device %x:%x (class %d) has no HID interfaces, ignoring\n",
                dev->path, vendor, product, dev_class);
        return;
    }

    if (!xhci_control(hc, dev, 0x00, USB_REQ_SET_CONFIGURATION, config_value, 0, 0, NULL))
    {
        kprintf("xhci: port %s: SET_CONFIGURATION failed\n", dev->path);
        dev->num_hid = 0;
        return;
    }

    int kept = 0;
    for (int i = 0; i < dev->num_hid; i++)
    {
        XhciHidEndpoint *ep = &dev->hid[i];

        if (ep->protocol == USB_HID_PROTO_KEYBOARD)
        {
            xhci_control(hc, dev, 0x21, USB_HID_REQ_SET_PROTOCOL, USB_HID_PROTOCOL_BOOT, ep->iface, 0, NULL);
            xhci_control(hc, dev, 0x21, USB_HID_REQ_SET_IDLE, 0, ep->iface, 0, NULL);
            dev->hid[kept++] = *ep;
            continue;
        }

        xhci_setup_hid_mouse(hc, dev, ep);
        if (ep->use_layout || ep->boot)
            dev->hid[kept++] = *ep;
    }
    dev->num_hid = kept;

    if (dev->num_hid == 0)
    {
        kprintf("xhci: port %s: device %x:%x has no usable keyboard/mouse interface\n",
                dev->path, vendor, product);
        return;
    }

    if (!xhci_configure_hid_endpoints(hc, dev))
    {
        kprintf("xhci: port %s: configure endpoint failed\n", dev->path);
        dev->num_hid = 0;
        return;
    }

    for (int i = 0; i < dev->num_hid; i++)
    {
        kprintf("xhci: port %s: USB %s (%x:%x) ready\n", dev->path,
                dev->hid[i].protocol == USB_HID_PROTO_KEYBOARD ? "keyboard" : "mouse",
                vendor, product);
        xhci_queue_interrupt(hc, dev, &dev->hid[i]);
    }
}

static void xhci_probe_port(XhciController *hc, uint8_t port)
{
    uint32_t off = XHCI_OP_PORTSC(port);
    uint32_t sc = xhci_read32(hc->op, off);

    if (!(sc & XHCI_PORTSC_CCS))
        return;

    if (!(sc & XHCI_PORTSC_PED))
    {
        xhci_write32(hc->op, off, (sc & ~XHCI_PORTSC_RW1C) | XHCI_PORTSC_PR);
        if (!xhci_wait_bits(hc->op, off, XHCI_PORTSC_PRC, XHCI_PORTSC_PRC, 500))
        {
            kprintf("xhci: port %d: reset timed out\n", port);
            return;
        }
        xhci_delay_ms(10);
        sc = xhci_read32(hc->op, off);
        if (!(sc & XHCI_PORTSC_PED))
        {
            kprintf("xhci: port %d: not enabled after reset\n", port);
            return;
        }
    }

    sc = xhci_read32(hc->op, off);
    xhci_write32(hc->op, off, (sc & ~XHCI_PORTSC_RW1C) | (sc & XHCI_PORTSC_CHANGE));

    xhci_setup_device(hc, NULL, port, (uint8_t)((sc >> 10) & 0xF));
}

static void xhci_bios_handoff(XhciController *hc, uint32_t hccparams1)
{
    uint32_t xecp = (hccparams1 >> 16) & 0xFFFF;
    if (!xecp)
        return;

    volatile uint32_t *cap = (volatile uint32_t *)(hc->cap + xecp * 4);
    for (;;)
    {
        uint32_t val = cap[0];
        if ((val & 0xFF) == XHCI_XCAP_LEGACY)
        {
            if (val & XHCI_LEGACY_BIOS_OWNED)
            {
                ((volatile uint8_t *)cap)[3] = 1;

                int released = 0;
                for (int i = 0; i < 1000 && !released; i++)
                {
                    released = !(cap[0] & XHCI_LEGACY_BIOS_OWNED);
                    if (!released)
                        xhci_delay_ms(1);
                }

                if (!released)
                {
                    kprintf("xhci: BIOS did not release controller, taking it anyway\n");
                    ((volatile uint8_t *)cap)[2] = 0;
                }
            }

            cap[1] = (cap[1] & ((0x7u << 1) | (0xFFu << 5) | (0x7u << 17))) | (0x7u << 29);
            return;
        }

        uint32_t next = (val >> 8) & 0xFF;
        if (!next)
            return;
        cap += next;
    }
}

static void xhci_intel_port_switch(const PciDevice *pci)
{
    if (pci->vendor_id != 0x8086)
        return;

    switch (pci->device_id)
    {
        case 0x1E31: case 0x8C31: case 0x8CB1: case 0x9C31: case 0x9CB1:
            break;
        default:
            return;
    }

    pci_config_write32(pci->bus, pci->slot, pci->func, 0xD8,
                       pci_config_read32(pci->bus, pci->slot, pci->func, 0xDC));
    pci_config_write32(pci->bus, pci->slot, pci->func, 0xD0,
                       pci_config_read32(pci->bus, pci->slot, pci->func, 0xD4));
}

static uint64_t xhci_bar_size(const PciDevice *pci)
{
    uint16_t cmd = pci_config_read16(pci->bus, pci->slot, pci->func, PCI_REG_COMMAND);
    pci_config_write16(pci->bus, pci->slot, pci->func, PCI_REG_COMMAND, cmd & ~PCI_CMD_MEM_SPACE);

    pci_config_write32(pci->bus, pci->slot, pci->func, PCI_REG_BAR0, 0xFFFFFFFF);
    uint32_t mask = pci_config_read32(pci->bus, pci->slot, pci->func, PCI_REG_BAR0) & ~0xFu;
    pci_config_write32(pci->bus, pci->slot, pci->func, PCI_REG_BAR0, pci->bar[0]);

    pci_config_write16(pci->bus, pci->slot, pci->func, PCI_REG_COMMAND, cmd);

    uint64_t size = mask ? (uint64_t)(~mask + 1) : 0;
    return size ? size : 0x10000;
}

static int xhci_init_controller(XhciController *hc, const PciDevice *pci)
{
    memset(hc, 0, sizeof(*hc));

    if (pci_bar_is_io(pci->bar[0]))
    {
        kprintf("xhci: BAR0 is not a memory BAR, skipping\n");
        return 0;
    }

    uint64_t bar_size = xhci_bar_size(pci);
    pci_enable_device(pci);
    xhci_intel_port_switch(pci);

    hc->cap = (volatile uint8_t *)vmm_map_mmio(pci_bar_addr(pci, 0), bar_size);

    uint8_t  caplength  = *(volatile uint8_t *)(hc->cap + XHCI_CAP_CAPLENGTH);
    uint32_t hcsparams1 = xhci_read32(hc->cap, XHCI_CAP_HCSPARAMS1);
    uint32_t hcsparams2 = xhci_read32(hc->cap, XHCI_CAP_HCSPARAMS2);
    uint32_t hccparams1 = xhci_read32(hc->cap, XHCI_CAP_HCCPARAMS1);

    if (hcsparams1 == 0xFFFFFFFF)
    {
        kprintf("xhci: registers read as all ones (device powered off?), skipping\n");
        return 0;
    }

    hc->op = hc->cap + caplength;
    hc->rt = hc->cap + (xhci_read32(hc->cap, XHCI_CAP_RTSOFF) & ~0x1Fu);
    hc->db = (volatile uint32_t *)(hc->cap + (xhci_read32(hc->cap, XHCI_CAP_DBOFF) & ~0x3u));

    hc->max_slots = MIN(hcsparams1 & 0xFF, XHCI_MAX_SLOTS);
    hc->max_ports = (hcsparams1 >> 24) & 0xFF;
    hc->ctx_size  = (hccparams1 & XHCI_HCC_CSZ) ? 64 : 32;

    xhci_bios_handoff(hc, hccparams1);

    xhci_write32(hc->op, XHCI_OP_USBCMD, xhci_read32(hc->op, XHCI_OP_USBCMD) & ~XHCI_CMD_RUN);
    if (!xhci_wait_bits(hc->op, XHCI_OP_USBSTS, XHCI_STS_HCH, XHCI_STS_HCH, 100))
    {
        kprintf("xhci: controller did not halt\n");
        return 0;
    }

    xhci_write32(hc->op, XHCI_OP_USBCMD, XHCI_CMD_HCRST);
    xhci_delay_ms(1);
    if (!xhci_wait_bits(hc->op, XHCI_OP_USBCMD, XHCI_CMD_HCRST, 0, 1000) ||
        !xhci_wait_bits(hc->op, XHCI_OP_USBSTS, XHCI_STS_CNR, 0, 1000))
    {
        kprintf("xhci: controller reset timed out\n");
        return 0;
    }

    xhci_write32(hc->op, XHCI_OP_CONFIG, hc->max_slots);
    xhci_write32(hc->op, XHCI_OP_DNCTRL, 0);

    uint64_t dcbaa_phys;
    hc->dcbaa = (uint64_t *)xhci_alloc_dma(&dcbaa_phys);
    if (!hc->dcbaa)
        return 0;

    uint32_t scratch_count = (((hcsparams2 >> 21) & 0x1F) << 5) | ((hcsparams2 >> 27) & 0x1F);
    if (scratch_count)
    {
        uint64_t arr_phys;
        uint64_t *arr = (uint64_t *)xhci_alloc_dma(&arr_phys);
        if (!arr || scratch_count > PAGE_SIZE / sizeof(uint64_t))
        {
            kprintf("xhci: cannot set up %d scratchpad buffers\n", scratch_count);
            return 0;
        }
        for (uint32_t i = 0; i < scratch_count; i++)
            if (!xhci_alloc_dma(&arr[i]))
                return 0;
        hc->dcbaa[0] = arr_phys;
    }
    xhci_write64(hc->op, XHCI_OP_DCBAAP, dcbaa_phys);

    if (!xhci_ring_init(&hc->cmd))
        return 0;
    xhci_write64(hc->op, XHCI_OP_CRCR, hc->cmd.phys | 1);

    hc->events = (volatile XhciTrb *)xhci_alloc_dma(&hc->events_phys);
    hc->dma_buf = (uint8_t *)xhci_alloc_dma(&hc->dma_buf_phys);
    uint64_t erst_phys;
    uint64_t *erst = (uint64_t *)xhci_alloc_dma(&erst_phys);
    if (!hc->events || !hc->dma_buf || !erst)
        return 0;

    if (!(hccparams1 & XHCI_HCC_AC64) &&
        (hc->events_phys >> 32 || hc->cmd.phys >> 32 || dcbaa_phys >> 32))
        kprintf("xhci: warning, controller is 32-bit only but DMA memory is above 4GiB\n");

    erst[0] = hc->events_phys;
    erst[1] = XHCI_RING_TRBS;
    hc->ev_dequeue = 0;
    hc->ev_cycle = 1;

    xhci_write32(hc->rt, XHCI_RT_IMAN, 0);
    xhci_write32(hc->rt, XHCI_RT_ERSTSZ, 1);
    xhci_write64(hc->rt, XHCI_RT_ERDP, hc->events_phys);
    xhci_write64(hc->rt, XHCI_RT_ERSTBA, erst_phys);

    xhci_write32(hc->op, XHCI_OP_USBCMD, XHCI_CMD_RUN);
    if (!xhci_wait_bits(hc->op, XHCI_OP_USBSTS, XHCI_STS_HCH, 0, 100))
    {
        kprintf("xhci: controller failed to start\n");
        return 0;
    }

    kprintf("xhci: controller %x:%x running, %d ports, %d slots, %d-byte contexts\n",
            pci->vendor_id, pci->device_id, hc->max_ports, hc->max_slots, hc->ctx_size);

    for (uint32_t p = 1; p <= hc->max_ports; p++)
    {
        uint32_t sc = xhci_read32(hc->op, XHCI_OP_PORTSC(p));
        if (!(sc & XHCI_PORTSC_PP))
            xhci_write32(hc->op, XHCI_OP_PORTSC(p), (sc & ~XHCI_PORTSC_RW1C) | XHCI_PORTSC_PP);
    }

    xhci_delay_ms(300);

    for (uint32_t p = 1; p <= hc->max_ports; p++)
        xhci_probe_port(hc, (uint8_t)p);

    return 1;
}

typedef struct
{
    PciDevice devs[XHCI_MAX_CONTROLLERS];
    int count;
}
XhciPciScan;

static void xhci_pci_cb(const PciDevice *dev, void *ctx_)
{
    XhciPciScan *ctx = (XhciPciScan *)ctx_;
    if (dev->class_code != 0x0C || dev->subclass != 0x03)
        return;

    if (dev->prog_if != 0x30)
        return;
    if (ctx->count == XHCI_MAX_CONTROLLERS)
    {
        kprintf("xhci: too many controllers, skipping this one\n");
        return;
    }
    ctx->devs[ctx->count++] = *dev;
}

int xhci_init(void)
{
    XhciPciScan scan;
    scan.count = 0;
    pci_enumerate(xhci_pci_cb, &scan);

    if (scan.count == 0)
    {
        kprintf("xhci: no controller found\n");
        return 0;
    }

    for (int i = 0; i < scan.count; i++)
        if (xhci_init_controller(&xhci_controllers[xhci_controller_count], &scan.devs[i]))
            xhci_controller_count++;

    xhci_ready = xhci_controller_count > 0;
    return xhci_controller_count;
}

void xhci_poll(void)
{
    if (!xhci_ready)
        return;

    for (int c = 0; c < xhci_controller_count; c++)
    {
        XhciController *hc = &xhci_controllers[c];

        XhciTrb ev;
        while (xhci_next_event(hc, &ev))
            if (TRB_GET_TYPE(ev.control) == TRB_EV_TRANSFER)
                xhci_handle_transfer(hc, &ev);

        for (uint32_t s = 1; s <= hc->max_slots; s++)
        {
            XhciDevice *dev = hc->slots[s];
            if (!dev)
                continue;
            for (int i = 0; i < dev->num_hid; i++)
                if (dev->hid[i].protocol == USB_HID_PROTO_KEYBOARD)
                    usb_hid_keyboard_tick(&dev->hid[i].kbd);
        }
    }
}

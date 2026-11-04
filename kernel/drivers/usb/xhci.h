#define XHCI_MAX_CONTROLLERS 8
#define XHCI_MAX_SLOTS       32
#define XHCI_MAX_HID_EPS     8

#define XHCI_RING_TRBS       256

typedef struct
{
    uint64_t param;
    uint32_t status;
    uint32_t control;
}
XhciTrb;

typedef struct
{
    volatile XhciTrb *trbs;
    uint64_t          phys;
    uint32_t          enqueue;
    uint32_t          cycle;
}
XhciRing;

typedef struct
{
    uint8_t        dci;
    uint8_t        iface;
    uint8_t        protocol;
    uint8_t        boot;
    uint16_t       report_desc_len;
    uint8_t        interval;
    uint16_t       max_packet;
    uint8_t       *buf;
    uint64_t       buf_phys;
    XhciRing       ring;
    UsbHidKeyboard kbd;
    int               use_layout;
    UsbHidMouseLayout layout;
}
XhciHidEndpoint;

#define XHCI_MAX_HUB_TIER    5

typedef struct
{
    uint8_t         slot_id;
    uint8_t         port;
    uint8_t         speed;
    uint8_t         tier;
    uint32_t        route;
    uint8_t         tt_slot;
    uint8_t         tt_port;
    char            path[24];
    uint8_t        *in_ctx;
    uint64_t        in_ctx_phys;
    uint8_t        *out_ctx;
    XhciRing        ep0;
    int             num_hid;
    XhciHidEndpoint hid[XHCI_MAX_HID_EPS];
}
XhciDevice;

typedef struct
{
    volatile uint8_t  *cap;
    volatile uint8_t  *op;
    volatile uint8_t  *rt;
    volatile uint32_t *db;

    uint32_t ctx_size;
    uint32_t max_slots;
    uint32_t max_ports;

    uint64_t *dcbaa;

    XhciRing cmd;

    volatile XhciTrb *events;
    uint64_t          events_phys;
    uint32_t          ev_dequeue;
    uint32_t          ev_cycle;

    uint8_t  *dma_buf;
    uint64_t  dma_buf_phys;

    XhciDevice *slots[XHCI_MAX_SLOTS + 1];
}
XhciController;

int  xhci_init(void);

void xhci_poll(void);

typedef struct
{
    const char *name;
    int  (*init)(void);
    void (*poll)(void);
    void (*set_rx_callback)(NicRxCallback cb);
    void (*send)(const uint8_t *frame, uint16_t len);
    const uint8_t *(*mac)(void);
}
NicDriver;

static const NicDriver nic_drivers[] = {
    { "rtl8169", rtl8169_init, rtl8169_poll, rtl8169_set_rx_callback, rtl8169_send, rtl8169_mac },
    { "rtl8139", rtl8139_init, rtl8139_poll, rtl8139_set_rx_callback, rtl8139_send, rtl8139_mac },
};

static const NicDriver *nic_active;

static const uint8_t nic_no_mac[6];

int nic_init(void)
{
    for (size_t i = 0; i < ARRAY_LEN(nic_drivers); i++)
    {
        if (nic_drivers[i].init())
        {
            nic_active = &nic_drivers[i];
            kprintf("nic: using %s\n", nic_active->name);
            return 1;
        }
    }
    kprintf("nic: no supported network card found\n");
    return 0;
}

void nic_poll(void)
{
    if (nic_active)
        nic_active->poll();
}

void nic_set_rx_callback(NicRxCallback cb)
{
    if (nic_active)
        nic_active->set_rx_callback(cb);
}

void nic_send(const uint8_t *frame, uint16_t len)
{
    if (nic_active)
        nic_active->send(frame, len);
}

const uint8_t *nic_mac(void)
{
    return nic_active ? nic_active->mac() : nic_no_mac;
}

#define RTL8139_VENDOR_ID 0x10EC
#define RTL8139_DEVICE_ID 0x8139

typedef void (*Rtl8139RxCallback)(const uint8_t *frame, uint16_t len);

int rtl8139_init(void);
void rtl8139_poll(void);

void rtl8139_set_rx_callback(Rtl8139RxCallback cb);

void rtl8139_send(const uint8_t *frame, uint16_t len);

const uint8_t *rtl8139_mac(void);
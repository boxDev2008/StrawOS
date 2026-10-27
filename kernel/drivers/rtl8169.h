#define RTL8169_VENDOR_ID 0x10EC

typedef void (*Rtl8169RxCallback)(const uint8_t *frame, uint16_t len);

int rtl8169_init(void);
void rtl8169_poll(void);

void rtl8169_set_rx_callback(Rtl8169RxCallback cb);

void rtl8169_send(const uint8_t *frame, uint16_t len);

const uint8_t *rtl8169_mac(void);

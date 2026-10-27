typedef void (*NicRxCallback)(const uint8_t *frame, uint16_t len);

int nic_init(void);
void nic_poll(void);

void nic_set_rx_callback(NicRxCallback cb);

void nic_send(const uint8_t *frame, uint16_t len);

const uint8_t *nic_mac(void);

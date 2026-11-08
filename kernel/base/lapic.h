void lapic_init(void);
void lapic_enable(void);
void lapic_eoi(void);
uint32_t lapic_id(void);

void lapic_timer_init(uint32_t hz);
void lapic_timer_start(uint32_t hz);
uint64_t lapic_tsc_per_ms(void);
void lapic_send_ipi(uint32_t lapic_id, uint8_t vector);
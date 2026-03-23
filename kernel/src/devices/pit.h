#pragma once

#include <stdint.h>

void pit_init(void);
uint64_t pit_get_ticks(void);
uint64_t pit_get_ticks_ms(void);
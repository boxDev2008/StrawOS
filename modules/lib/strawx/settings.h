#pragma once

#include <stdbool.h>
#include <stddef.h>

#define SX_SETTINGS_PATH "/config/settings.conf"
#define SX_SETTING_MAX   192

bool sx_setting_get(const char *key, char *out, size_t size);
int  sx_setting_get_int(const char *key, int fallback);
bool sx_setting_set(const char *key, const char *value);
bool sx_setting_set_int(const char *key, int value);
void sx_settings_apply(void);

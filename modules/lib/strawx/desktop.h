#pragma once

#include <stdbool.h>

#define SX_DESKTOP_DIR "/desktop"

typedef struct
{
    char name[64];
    char comment[96];
    char icon[128];
    char exec[256];
}
SxDesktopEntry;

bool sx_desktop_entry_load(const char *path, SxDesktopEntry *out);
bool sx_desktop_entry_launch(const SxDesktopEntry *entry);
bool sx_is_desktop_file(const char *name);

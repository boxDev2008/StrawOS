#pragma once

#include <stdbool.h>

#include "vp8dec.h"

typedef struct Watch Watch;

Watch *watch_open(const char *title);
void   watch_close(Watch *w);
bool   watch_pump(Watch *w);
void   watch_frame(Watch *w, const Vp8Image *img);
void   watch_status(Watch *w, const char *msg);

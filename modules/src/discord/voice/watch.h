#pragma once

#include <stdbool.h>

#include "videodec.h"

typedef struct Watch Watch;

Watch *watch_open(const char *title);
void   watch_close(Watch *w);
bool   watch_pump(Watch *w);
void   watch_frame(Watch *w, const VideoImage *img);
void   watch_status(Watch *w, const char *msg);

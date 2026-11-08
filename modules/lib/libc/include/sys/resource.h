#pragma once

#include <types.h>

#define PRIO_PROCESS 0
#define PRIO_PGRP    1
#define PRIO_USER    2

#define PRIO_MIN (-20)
#define PRIO_MAX 19

// who == 0 means the calling task. Only PRIO_PROCESS is supported.
int getpriority(int which, int who);
int setpriority(int which, int who, int prio);

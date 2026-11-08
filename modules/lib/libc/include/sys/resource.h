#pragma once

#include <types.h>

#define PRIO_PROCESS 0
#define PRIO_PGRP    1
#define PRIO_USER    2

#define PRIO_MIN (-20)
#define PRIO_MAX 19

int getpriority(int which, int who);
int setpriority(int which, int who, int prio);

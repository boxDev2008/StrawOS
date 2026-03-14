#pragma once

#include <stdint.h>

typedef int64_t time_t;

struct tm
{
    int tm_sec;   /* seconds [0,59] */
    int tm_min;   /* minutes [0,59] */
    int tm_hour;  /* hour [0,23] */
    int tm_mday;  /* day of month [1,31] */
    int tm_mon;   /* month [0,11] */
    int tm_year;  /* years since 1900 */
    int tm_wday;  /* days since Sunday [0,6] */
    int tm_yday;  /* days since Jan 1 [0,365] */
    int tm_isdst; /* daylight saving flag */
};

time_t time(time_t *t);
struct tm *localtime(const time_t *timer);
struct tm *gmtime(const time_t *timer);
char *asctime(const struct tm *timeptr);
char *ctime(const time_t *timer);
unsigned int sleep(unsigned int seconds);
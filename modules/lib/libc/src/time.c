#include <time.h>
#include <stdio.h>

static time_t fake_clock = 0;

time_t time(time_t *t)
{
    fake_clock++;

    if (t)
        *t = fake_clock;

    return fake_clock;
}

unsigned int sleep(unsigned int seconds)
{
    volatile unsigned long i;
    for (unsigned int s = 0; s < seconds; s++)
        for (i = 0; i < 100000000; i++);

    return 0;
}

struct tm *gmtime(const time_t *timer)
{
    static struct tm t;

    t.tm_sec  = *timer % 60;
    t.tm_min  = (*timer / 60) % 60;
    t.tm_hour = (*timer / 3600) % 24;

    t.tm_mday = 1;
    t.tm_mon  = 0;
    t.tm_year = 70;

    return &t;
}

char *asctime(const struct tm *timeptr)
{
    static char buf[64];

    sprintf(buf, "%02d:%02d:%02d\n",
        timeptr->tm_hour,
        timeptr->tm_min,
        timeptr->tm_sec);

    return buf;
}

char *ctime(const time_t *timer)
{
    return asctime(gmtime(timer));
}
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <types.h>

typedef int64_t clock_t;

#define CLOCKS_PER_SEC 1000

struct tm
{
    int tm_sec;
    int tm_min;
    int tm_hour;
    int tm_mday;
    int tm_mon;
    int tm_year;
    int tm_wday;
    int tm_yday;
    int tm_isdst;
};

struct rtc_time
{
    uint16_t year;
    uint8_t  month;
    uint8_t  day;
    uint8_t  hour;
    uint8_t  minute;
    uint8_t  second;
    uint8_t  weekday;
};

struct meminfo
{
    uint64_t total_pages;
    uint64_t free_pages;
    uint64_t page_size;
};

#define CPUINFO_MAX_CPUS 64

struct cpuinfo
{
    uint64_t cpus;
    uint64_t total_ticks;
    uint64_t busy_ticks;
    uint64_t cpu_total_ticks[CPUINFO_MAX_CPUS];
    uint64_t cpu_busy_ticks[CPUINFO_MAX_CPUS];
};

#define TASKINFO_NAME_MAX 64

enum task_state
{
    TASK_STATE_RUNNING  = 0,
    TASK_STATE_READY    = 1,
    TASK_STATE_SLEEPING = 2,
    TASK_STATE_UNKNOWN  = 3
};

struct taskinfo
{
    uint64_t pid;
    uint8_t  state;
    uint8_t  kernel;
    uint64_t cpu_ticks;
    uint64_t mem_kb;
    char     name[TASKINFO_NAME_MAX];
};

uint64_t uptime_ms(void);
int rtc_time(struct rtc_time *out);
int meminfo(struct meminfo *out);
int cpuinfo(struct cpuinfo *out);
int tasklist(struct taskinfo *out, int max);
void yield(void);

time_t time(time_t *out);
clock_t clock(void);
double difftime(time_t a, time_t b);
time_t mktime(struct tm *tm);
struct tm *gmtime(const time_t *t);
struct tm *localtime(const time_t *t);
size_t strftime(char *buf, size_t size, const char *fmt, const struct tm *tm);
char *strptime(const char *s, const char *fmt, struct tm *tm);
time_t timegm(struct tm *tm);
struct tm *gmtime_r(const time_t *t, struct tm *out);
struct tm *localtime_r(const time_t *t, struct tm *out);
char *asctime(const struct tm *tm);
char *ctime(const time_t *t);

struct timespec
{
    time_t tv_sec;
    long   tv_nsec;
};

typedef int clockid_t;
#define CLOCK_REALTIME  0
#define CLOCK_MONOTONIC 1

int clock_gettime(clockid_t clk, struct timespec *ts);
int nanosleep(const struct timespec *req, struct timespec *rem);

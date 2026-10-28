uint64_t uptime_ms(void)
{
    return kcall(KCALL_UPTIME, 0, 0, 0, 0);
}

void yield(void)
{
    kcall(KCALL_YIELD, 0, 0, 0, 0);
}

static int read_device_text(const char *path, char *buf, size_t size)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return -1;

    size_t len = 0;
    for (;;)
    {
        ssize_t n = read(fd, buf + len, size - 1 - len);
        if (n <= 0)
            break;
        len += (size_t)n;
        if (len == size - 1)
            break;
    }
    close(fd);
    buf[len] = '\0';
    return len > 0 ? 0 : -1;
}

static const char *parse_uint(const char *s, uint64_t *out)
{
    uint64_t v = 0;
    if (*s < '0' || *s > '9')
        return NULL;
    while (*s >= '0' && *s <= '9')
        v = v * 10 + (uint64_t)(*s++ - '0');
    *out = v;
    return s;
}

static bool text_field(const char *text, const char *key, uint64_t *out)
{
    size_t key_len = strlen(key);
    for (const char *line = text; line && *line; )
    {
        if (strncmp(line, key, key_len) == 0 && line[key_len] == ':')
        {
            const char *v = line + key_len + 1;
            while (*v == ' ')
                v++;
            return parse_uint(v, out) != NULL;
        }
        line = strchr(line, '\n');
        if (line)
            line++;
    }
    return false;
}

int rtc_time(struct rtc_time *out)
{
    static const char *weekdays[7] = {
        "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday",
    };

    char buf[64];
    if (!out || read_device_text("/dev/rtc", buf, sizeof(buf)) != 0)
        return -1;

    uint64_t f[6];
    const char *p = buf;
    const char seps[6] = { '-', '-', ' ', ':', ':', ' ' };
    for (int i = 0; i < 6; i++)
    {
        p = parse_uint(p, &f[i]);
        if (!p || *p != seps[i])
            return -1;
        p++;
    }

    out->year = (uint16_t)f[0];
    out->month = (uint8_t)f[1];
    out->day = (uint8_t)f[2];
    out->hour = (uint8_t)f[3];
    out->minute = (uint8_t)f[4];
    out->second = (uint8_t)f[5];
    out->weekday = 0;
    for (int i = 0; i < 7; i++)
        if (strncmp(p, weekdays[i], strlen(weekdays[i])) == 0)
            out->weekday = (uint8_t)i;
    return 0;
}

int meminfo(struct meminfo *out)
{
    char buf[256];
    if (!out || read_device_text("/dev/meminfo", buf, sizeof(buf)) != 0)
        return -1;

    uint64_t total_kb, free_kb, page_size;
    if (!text_field(buf, "total_kb", &total_kb) || !text_field(buf, "free_kb", &free_kb) ||
        !text_field(buf, "page_size", &page_size) || page_size == 0)
        return -1;

    out->page_size = page_size;
    out->total_pages = total_kb * 1024 / page_size;
    out->free_pages = free_kb * 1024 / page_size;
    return 0;
}

int cpuinfo(struct cpuinfo *out)
{
    static char buf[4096];
    if (!out || read_device_text("/dev/cpuinfo", buf, sizeof(buf)) != 0)
        return -1;

    if (!text_field(buf, "cpus", &out->cpus) || out->cpus == 0)
        return -1;
    if (out->cpus > CPUINFO_MAX_CPUS)
        out->cpus = CPUINFO_MAX_CPUS;

    out->total_ticks = 0;
    out->busy_ticks = 0;
    for (uint64_t i = 0; i < out->cpus; i++)
    {
        char key[32];
        snprintf(key, sizeof(key), "cpu%llu_total", (unsigned long long)i);
        if (!text_field(buf, key, &out->cpu_total_ticks[i]))
            return -1;
        snprintf(key, sizeof(key), "cpu%llu_busy", (unsigned long long)i);
        if (!text_field(buf, key, &out->cpu_busy_ticks[i]))
            return -1;
        out->total_ticks += out->cpu_total_ticks[i];
        out->busy_ticks += out->cpu_busy_ticks[i];
    }
    return 0;
}

static int64_t days_from_civil(int64_t y, int m, int d)
{
    y -= m <= 2;
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    int64_t yoe = y - era * 400;
    int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

static bool is_leap(int64_t y)
{
    return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
}

static time_t time_base;
static uint64_t time_base_ms;
static bool time_base_set;

time_t time(time_t *out)
{
    uint64_t now = uptime_ms();
    if (!time_base_set || now - time_base_ms > 60000)
    {
        struct rtc_time r;
        if (rtc_time(&r) == 0)
        {
            time_base = (time_t)days_from_civil(r.year, r.month, r.day) * 86400 + r.hour * 3600 + r.minute * 60 + r.second;
            time_base_ms = now;
            time_base_set = true;
        }
    }
    time_t t = time_base_set ? time_base + (time_t)((now - time_base_ms) / 1000) : 0;
    if (out)
        *out = t;
    return t;
}

clock_t clock(void)
{
    return (clock_t)uptime_ms();
}

double difftime(time_t a, time_t b)
{
    return (double)(a - b);
}

time_t mktime(struct tm *tm)
{
    int64_t year = tm->tm_year + 1900;
    int64_t mon = tm->tm_mon;
    year += mon / 12;
    mon %= 12;
    if (mon < 0)
    {
        mon += 12;
        year--;
    }
    time_t t = (time_t)days_from_civil(year, (int)mon + 1, 1) * 86400;
    t += (time_t)(tm->tm_mday - 1) * 86400 + (time_t)tm->tm_hour * 3600 + (time_t)tm->tm_min * 60 + tm->tm_sec;
    struct tm *norm = gmtime(&t);
    if (norm)
        *tm = *norm;
    return t;
}

struct tm *gmtime(const time_t *t)
{
    static struct tm result;
    int64_t secs = *t;
    int64_t days = secs / 86400;
    int64_t rem = secs % 86400;
    if (rem < 0)
    {
        rem += 86400;
        days--;
    }

    result.tm_hour = (int)(rem / 3600);
    result.tm_min = (int)(rem / 60 % 60);
    result.tm_sec = (int)(rem % 60);
    result.tm_wday = (int)((days + 4) % 7);
    if (result.tm_wday < 0)
        result.tm_wday += 7;

    int64_t z = days + 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    int64_t doe = z - era * 146097;
    int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t y = yoe + era * 400;
    int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int64_t mp = (5 * doy + 2) / 153;
    int64_t d = doy - (153 * mp + 2) / 5 + 1;
    int64_t m = mp < 10 ? mp + 3 : mp - 9;
    y += m <= 2;

    result.tm_year = (int)(y - 1900);
    result.tm_mon = (int)(m - 1);
    result.tm_mday = (int)d;
    result.tm_yday = (int)(days - days_from_civil(y, 1, 1));
    result.tm_isdst = 0;
    return &result;
}

struct tm *localtime(const time_t *t)
{
    return gmtime(t);
}

size_t strftime(char *buf, size_t size, const char *fmt, const struct tm *tm)
{
    static const char *wday[] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };
    static const char *mon[] = { "January", "February", "March", "April", "May", "June", "July", "August",
                                 "September", "October", "November", "December" };
    size_t pos = 0;
    char tmp[64];

    for (; *fmt; fmt++)
    {
        if (*fmt != '%')
        {
            if (pos + 1 >= size)
                return 0;
            buf[pos++] = *fmt;
            continue;
        }

        fmt++;
        tmp[0] = '\0';
        switch (*fmt)
        {
            case 'a': snprintf(tmp, sizeof(tmp), "%.3s", wday[tm->tm_wday % 7]); break;
            case 'A': snprintf(tmp, sizeof(tmp), "%s", wday[tm->tm_wday % 7]); break;
            case 'b': case 'h': snprintf(tmp, sizeof(tmp), "%.3s", mon[tm->tm_mon % 12]); break;
            case 'B': snprintf(tmp, sizeof(tmp), "%s", mon[tm->tm_mon % 12]); break;
            case 'c': snprintf(tmp, sizeof(tmp), "%.3s %.3s %2d %02d:%02d:%02d %d", wday[tm->tm_wday % 7],
                               mon[tm->tm_mon % 12], tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec,
                               tm->tm_year + 1900); break;
            case 'd': snprintf(tmp, sizeof(tmp), "%02d", tm->tm_mday); break;
            case 'e': snprintf(tmp, sizeof(tmp), "%2d", tm->tm_mday); break;
            case 'D': snprintf(tmp, sizeof(tmp), "%02d/%02d/%02d", tm->tm_mon + 1, tm->tm_mday, tm->tm_year % 100); break;
            case 'F': snprintf(tmp, sizeof(tmp), "%d-%02d-%02d", tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday); break;
            case 'H': snprintf(tmp, sizeof(tmp), "%02d", tm->tm_hour); break;
            case 'I': snprintf(tmp, sizeof(tmp), "%02d", tm->tm_hour % 12 ? tm->tm_hour % 12 : 12); break;
            case 'j': snprintf(tmp, sizeof(tmp), "%03d", tm->tm_yday + 1); break;
            case 'm': snprintf(tmp, sizeof(tmp), "%02d", tm->tm_mon + 1); break;
            case 'M': snprintf(tmp, sizeof(tmp), "%02d", tm->tm_min); break;
            case 'n': snprintf(tmp, sizeof(tmp), "\n"); break;
            case 'p': snprintf(tmp, sizeof(tmp), "%s", tm->tm_hour < 12 ? "AM" : "PM"); break;
            case 'S': snprintf(tmp, sizeof(tmp), "%02d", tm->tm_sec); break;
            case 't': snprintf(tmp, sizeof(tmp), "\t"); break;
            case 'T': case 'X': snprintf(tmp, sizeof(tmp), "%02d:%02d:%02d", tm->tm_hour, tm->tm_min, tm->tm_sec); break;
            case 'R': snprintf(tmp, sizeof(tmp), "%02d:%02d", tm->tm_hour, tm->tm_min); break;
            case 'x': snprintf(tmp, sizeof(tmp), "%02d/%02d/%02d", tm->tm_mon + 1, tm->tm_mday, tm->tm_year % 100); break;
            case 'y': snprintf(tmp, sizeof(tmp), "%02d", tm->tm_year % 100); break;
            case 'Y': snprintf(tmp, sizeof(tmp), "%d", tm->tm_year + 1900); break;
            case 'w': snprintf(tmp, sizeof(tmp), "%d", tm->tm_wday); break;
            case 'u': snprintf(tmp, sizeof(tmp), "%d", tm->tm_wday ? tm->tm_wday : 7); break;
            case 'Z': snprintf(tmp, sizeof(tmp), "UTC"); break;
            case 'z': snprintf(tmp, sizeof(tmp), "+0000"); break;
            case 's': snprintf(tmp, sizeof(tmp), "%lld", (long long)mktime((struct tm *)tm)); break;
            case '%': snprintf(tmp, sizeof(tmp), "%%"); break;
            case '\0': fmt--; break;
            default: snprintf(tmp, sizeof(tmp), "%%%c", *fmt); break;
        }

        size_t len = strlen(tmp);
        if (pos + len >= size)
            return 0;
        memcpy(buf + pos, tmp, len);
        pos += len;
    }

    if (pos >= size)
        return 0;
    buf[pos] = '\0';
    return pos;
}

struct tm *gmtime_r(const time_t *t, struct tm *out)
{
    *out = *gmtime(t);
    return out;
}

struct tm *localtime_r(const time_t *t, struct tm *out)
{
    return gmtime_r(t, out);
}

time_t timegm(struct tm *tm)
{
    return mktime(tm);
}

char *asctime(const struct tm *tm)
{
    static char buf[32];
    strftime(buf, sizeof(buf), "%a %b %e %H:%M:%S %Y\n", tm);
    return buf;
}

char *ctime(const time_t *t)
{
    return asctime(gmtime(t));
}

static int strptime_number(const char **s, int min_digits, int max_digits, int *out)
{
    int value = 0, n = 0;
    while (**s == ' ')
        (*s)++;
    if (**s == '+' || **s == '-')
        return 0;
    while (n < max_digits && **s >= '0' && **s <= '9')
    {
        value = value * 10 + (**s - '0');
        (*s)++;
        n++;
    }
    if (n < min_digits)
        return 0;
    *out = value;
    return 1;
}

static int strptime_name(const char **s, const char *const *names, int count, int *out)
{
    for (int i = 0; i < count; i++)
    {
        size_t full = strlen(names[i]);
        if (strncasecmp(*s, names[i], full) == 0)
        {
            *s += full;
            *out = i;
            return 1;
        }
        if (strncasecmp(*s, names[i], 3) == 0)
        {
            *s += 3;
            *out = i;
            return 1;
        }
    }
    return 0;
}

char *strptime(const char *s, const char *fmt, struct tm *tm)
{
    static const char *const wday[] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };
    static const char *const mon[] = { "January", "February", "March", "April", "May", "June", "July", "August",
                                       "September", "October", "November", "December" };
    int v;
    for (; *fmt; fmt++)
    {
        if (*fmt == ' ')
        {
            while (*s == ' ' || *s == '\t')
                s++;
            continue;
        }
        if (*fmt != '%')
        {
            if (*s != *fmt)
                return NULL;
            s++;
            continue;
        }
        fmt++;
        switch (*fmt)
        {
            case 'a': case 'A':
                if (!strptime_name(&s, wday, 7, &tm->tm_wday)) return NULL;
                break;
            case 'b': case 'B': case 'h':
                if (!strptime_name(&s, mon, 12, &tm->tm_mon)) return NULL;
                break;
            case 'd': case 'e':
                if (!strptime_number(&s, 1, 2, &tm->tm_mday)) return NULL;
                break;
            case 'H':
                if (!strptime_number(&s, 1, 2, &tm->tm_hour)) return NULL;
                break;
            case 'M':
                if (!strptime_number(&s, 1, 2, &tm->tm_min)) return NULL;
                break;
            case 'S':
                if (!strptime_number(&s, 1, 2, &tm->tm_sec)) return NULL;
                break;
            case 'm':
                if (!strptime_number(&s, 1, 2, &v)) return NULL;
                tm->tm_mon = v - 1;
                break;
            case 'y':
                if (!strptime_number(&s, 1, 2, &v)) return NULL;
                tm->tm_year = v < 69 ? v + 100 : v;
                break;
            case 'Y':
                if (!strptime_number(&s, 1, 4, &v)) return NULL;
                tm->tm_year = v - 1900;
                break;
            case 'j':
                if (!strptime_number(&s, 1, 3, &v)) return NULL;
                tm->tm_yday = v - 1;
                break;
            case 'T':
                s = strptime(s, "%H:%M:%S", tm);
                if (!s) return NULL;
                break;
            case 'Z':
                while (*s && *s != ' ')
                    s++;
                break;
            case 'z':
                if (*s == '+' || *s == '-')
                    s++;
                while (*s >= '0' && *s <= '9')
                    s++;
                break;
            case 'n': case 't':
                while (*s == ' ' || *s == '\t' || *s == '\n')
                    s++;
                break;
            case '%':
                if (*s != '%') return NULL;
                s++;
                break;
            default:
                return NULL;
        }
    }
    return (char *)s;
}

static time_t clock_base_sec;
static uint64_t clock_base_ms;
static bool clock_base_set;

int clock_gettime(clockid_t clk, struct timespec *ts)
{
    uint64_t ms = uptime_ms();
    if (clk == CLOCK_MONOTONIC)
    {
        ts->tv_sec = (time_t)(ms / 1000);
        ts->tv_nsec = (long)(ms % 1000) * 1000000L;
        return 0;
    }
    if (!clock_base_set)
    {
        clock_base_sec = time(NULL);
        clock_base_ms = ms;
        clock_base_set = true;
    }
    uint64_t elapsed = ms - clock_base_ms;
    ts->tv_sec = clock_base_sec + (time_t)(elapsed / 1000);
    ts->tv_nsec = (long)(elapsed % 1000) * 1000000L;
    return 0;
}

int nanosleep(const struct timespec *req, struct timespec *rem)
{
    uint64_t ms = (uint64_t)req->tv_sec * 1000 + (uint64_t)req->tv_nsec / 1000000;
    if (ms)
        wait((uint32_t)ms);
    else
        yield();
    if (rem)
    {
        rem->tv_sec = 0;
        rem->tv_nsec = 0;
    }
    return 0;
}

int gettimeofday(struct timeval *tv, void *tz)
{
    (void)tz;
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    tv->tv_sec = ts.tv_sec;
    tv->tv_usec = ts.tv_nsec / 1000;
    return 0;
}

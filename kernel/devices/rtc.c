#define CMOS_ADDR 0x70
#define CMOS_DATA 0x71

static uint8_t cmos_read(uint8_t reg)
{
    outb(CMOS_ADDR, reg);
    return inb(CMOS_DATA);
}

static void rtc_read_raw(uint8_t r[7])
{
    while (cmos_read(0x0A) & 0x80)
        pause();

    r[0] = cmos_read(0x00);
    r[1] = cmos_read(0x02);
    r[2] = cmos_read(0x04);
    r[3] = cmos_read(0x07);
    r[4] = cmos_read(0x08);
    r[5] = cmos_read(0x09);
    r[6] = cmos_read(0x06);
}

static uint8_t bcd_to_bin(uint8_t v)
{
    return (uint8_t)((v & 0x0F) + (v >> 4) * 10);
}

int rtc_read(RtcTime *out)
{
    if (!out)
        return -1;

    uint8_t a[7], b[7];
    uint64_t flags = irq_save();

    rtc_read_raw(a);
    for (;;)
    {
        rtc_read_raw(b);
        if (memcmp(a, b, sizeof(a)) == 0)
            break;
        memcpy(a, b, sizeof(a));
    }

    uint8_t status_b = cmos_read(0x0B);
    irq_restore(flags);

    bool pm = (a[2] & 0x80) != 0;
    a[2] &= 0x7F;

    if (!(status_b & 0x04))
        for (int i = 0; i < 7; i++)
            a[i] = bcd_to_bin(a[i]);

    if (!(status_b & 0x02))
    {
        a[2] %= 12;
        if (pm)
            a[2] += 12;
    }

    out->second  = a[0];
    out->minute  = a[1];
    out->hour    = a[2];
    out->day     = a[3];
    out->month   = a[4];
    out->year    = (uint16_t)(2000 + a[5]);
    out->weekday = (uint8_t)((a[6] + 6) % 7);
    return 0;
}

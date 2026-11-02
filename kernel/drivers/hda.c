#define HDA_REG_GCAP      0x00
#define HDA_REG_VMIN      0x02
#define HDA_REG_VMAJ      0x03
#define HDA_REG_GCTL      0x08
#define HDA_REG_STATESTS  0x0E
#define HDA_REG_INTCTL    0x20
#define HDA_REG_CORBLBASE 0x40
#define HDA_REG_CORBUBASE 0x44
#define HDA_REG_CORBWP    0x48
#define HDA_REG_CORBRP    0x4A
#define HDA_REG_CORBCTL   0x4C
#define HDA_REG_CORBSIZE  0x4E
#define HDA_REG_RIRBLBASE 0x50
#define HDA_REG_RIRBUBASE 0x54
#define HDA_REG_RIRBWP    0x58
#define HDA_REG_RINTCNT   0x5A
#define HDA_REG_RIRBCTL   0x5C
#define HDA_REG_RIRBSTS   0x5D
#define HDA_REG_RIRBSIZE  0x5E
#define HDA_REG_DPLBASE   0x70
#define HDA_REG_DPUBASE   0x74
#define HDA_REG_SD_BASE   0x80
#define HDA_SD_STRIDE     0x20

#define HDA_GCTL_CRST     (1u << 0)

#define HDA_SD_CTL        0x00
#define HDA_SD_STS        0x03
#define HDA_SD_LPIB       0x04
#define HDA_SD_CBL        0x08
#define HDA_SD_LVI        0x0C
#define HDA_SD_FMT        0x12
#define HDA_SD_BDPL       0x18
#define HDA_SD_BDPU       0x1C

#define HDA_SD_CTL_SRST   (1u << 0)
#define HDA_SD_CTL_RUN    (1u << 1)
#define HDA_SD_STS_CLEAR  0x1C

#define HDA_FORMAT        0x0011

#define HDA_VERB_GET_PARAM      0xF00
#define HDA_VERB_GET_CONN_LIST  0xF02
#define HDA_VERB_SET_CONN_SEL   0x701
#define HDA_VERB_SET_POWER      0x705
#define HDA_VERB_SET_STREAM     0x706
#define HDA_VERB_SET_PIN_CTL    0x707
#define HDA_VERB_SET_EAPD       0x70C
#define HDA_VERB_GET_CONFIG     0xF1C
#define HDA_VERB_SET_FORMAT     0x2
#define HDA_VERB_SET_AMP        0x3

#define HDA_PARAM_VENDOR        0x00
#define HDA_PARAM_NODE_COUNT    0x04
#define HDA_PARAM_FG_TYPE       0x05
#define HDA_PARAM_WIDGET_CAP    0x09
#define HDA_PARAM_PIN_CAP       0x0C
#define HDA_PARAM_IN_AMP_CAP    0x0D
#define HDA_PARAM_CONN_LEN      0x0E
#define HDA_PARAM_OUT_AMP_CAP   0x12

#define HDA_FG_AUDIO            0x01

#define HDA_WCAP_IN_AMP         (1u << 1)
#define HDA_WCAP_OUT_AMP        (1u << 2)
#define HDA_WCAP_AMP_OVERRIDE   (1u << 3)
#define HDA_WCAP_CONN_LIST      (1u << 8)
#define HDA_WCAP_DIGITAL        (1u << 9)
#define HDA_WCAP_TYPE(c)        (((c) >> 20) & 0xF)

#define HDA_WIDGET_OUTPUT       0x0
#define HDA_WIDGET_INPUT        0x1
#define HDA_WIDGET_MIXER        0x2
#define HDA_WIDGET_SELECTOR     0x3
#define HDA_WIDGET_PIN          0x4

#define HDA_PINCAP_HP_DRIVE     (1u << 3)
#define HDA_PINCAP_OUT          (1u << 4)
#define HDA_PINCAP_IN           (1u << 5)
#define HDA_PINCAP_VREF_50      (1u << 9)
#define HDA_PINCAP_VREF_80      (1u << 12)
#define HDA_PINCAP_EAPD         (1u << 16)

#define HDA_PINCTL_VREF_50      0x01
#define HDA_PINCTL_VREF_80      0x04
#define HDA_PINCTL_IN           0x20
#define HDA_PINCTL_OUT          0x40
#define HDA_PINCTL_HP           0x80

#define HDA_CFG_CONN(c)         (((c) >> 30) & 0x3)
#define HDA_CFG_DEVICE(c)       (((c) >> 20) & 0xF)
#define HDA_CONN_NONE           0x1

#define HDA_DEV_LINE_OUT        0x0
#define HDA_DEV_SPEAKER         0x1
#define HDA_DEV_HP_OUT          0x2
#define HDA_DEV_LINE_IN         0x8
#define HDA_DEV_MIC_IN          0xA

#define HDA_AMP_SET_OUT         (1u << 15)
#define HDA_AMP_SET_IN          (1u << 14)
#define HDA_AMP_SET_LEFT        (1u << 13)
#define HDA_AMP_SET_RIGHT       (1u << 12)

#define HDA_MAX_NODES     256
#define HDA_MAX_CONN      32
#define HDA_MAX_PATH      6
#define HDA_MAX_OUT_PINS  4

#define HDA_PERIOD        (HDA_RATE / 100 * HDA_FRAME)
#define HDA_PERIODS       32
#define HDA_BUF_SIZE      (HDA_PERIOD * HDA_PERIODS)
#define HDA_BUF_PAGES     DIV_CEIL(HDA_BUF_SIZE, PAGE_SIZE)

#define HDA_PLAY_LEAD     (HDA_PERIOD * 4)
#define HDA_CAPTURE_KEEP  (HDA_BUF_SIZE - 4 * HDA_PERIOD)

#define HDA_CLIENT_RING   32768
#define HDA_IDLE_STOP_MS  2000

#define HDA_STREAM_TAG_OUT 1
#define HDA_STREAM_TAG_IN  2

_Static_assert(HDA_PERIOD % 128 == 0, "BDL buffers must stay 128-byte aligned");
_Static_assert(HDA_CLIENT_RING % HDA_FRAME == 0, "client ring must hold whole frames");

typedef struct PACKED
{
    uint64_t addr;
    uint32_t len;
    uint32_t flags;
}
HdaBdlEntry;

typedef struct
{
    uint32_t caps;
    uint32_t pin_caps;
    uint32_t config;
    uint32_t in_amp;
    uint32_t out_amp;
    uint8_t  conn_count;
    uint8_t  conn[HDA_MAX_CONN];
}
HdaWidget;

typedef struct
{
    int     len;
    uint8_t nid[HDA_MAX_PATH];
    uint8_t sel[HDA_MAX_PATH];
}
HdaPath;

typedef struct
{
    volatile uint8_t *regs;
    uint8_t     *buf;
    HdaBdlEntry *bdl;
    uint8_t      tag;
    bool         running;
    uint32_t     last_lpib;
    uint64_t     pos;
}
HdaStream;

typedef struct HdaPlayClient
{
    uint8_t  ring[HDA_CLIENT_RING];
    uint64_t head;
    uint64_t tail;
    bool     closing;
    struct HdaPlayClient *next;
}
HdaPlayClient;

typedef struct HdaCaptureClient
{
    uint64_t pos;
    struct HdaCaptureClient *next;
}
HdaCaptureClient;

typedef struct
{
    HdaPlayClient    *play;
    HdaCaptureClient *capture;
}
HdaFile;

static struct
{
    volatile uint8_t *mmio;
    bool      ready;
    bool      can_play;
    bool      can_capture;
    bool      dma64;

    volatile uint32_t *corb;
    volatile uint64_t *rirb;
    uint16_t  corb_entries;
    uint16_t  rirb_entries;
    uint16_t  rirb_rp;

    uint8_t   codec;
    uint8_t   afg;
    uint32_t  afg_in_amp;
    uint32_t  afg_out_amp;
    HdaWidget widgets[HDA_MAX_NODES];
    bool      present[HDA_MAX_NODES];

    HdaStream out;
    HdaStream in;

    uint64_t  fill;
    uint64_t  idle_since;
    uint32_t  volume;

    HdaPlayClient    *play_clients;
    HdaCaptureClient *capture_clients;
}
hda;

static uint8_t  hda_rd8(uint32_t reg)  { return *(volatile uint8_t  *)(hda.mmio + reg); }
static uint16_t hda_rd16(uint32_t reg) { return *(volatile uint16_t *)(hda.mmio + reg); }
static uint32_t hda_rd32(uint32_t reg) { return *(volatile uint32_t *)(hda.mmio + reg); }
static void hda_wr8(uint32_t reg, uint8_t v)   { *(volatile uint8_t  *)(hda.mmio + reg) = v; }
static void hda_wr16(uint32_t reg, uint16_t v) { *(volatile uint16_t *)(hda.mmio + reg) = v; }
static void hda_wr32(uint32_t reg, uint32_t v) { *(volatile uint32_t *)(hda.mmio + reg) = v; }

static uint32_t sd_rd32(HdaStream *s, uint32_t reg) { return *(volatile uint32_t *)(s->regs + reg); }
static void sd_wr8(HdaStream *s, uint32_t reg, uint8_t v)   { *(volatile uint8_t  *)(s->regs + reg) = v; }
static void sd_wr16(HdaStream *s, uint32_t reg, uint16_t v) { *(volatile uint16_t *)(s->regs + reg) = v; }
static void sd_wr32(HdaStream *s, uint32_t reg, uint32_t v) { *(volatile uint32_t *)(s->regs + reg) = v; }

static bool hda_wait_bits(uint32_t reg, uint32_t mask, uint32_t want, int width)
{
    for (int i = 0; i < 1000; i++)
    {
        uint32_t v = width == 8 ? hda_rd8(reg) : width == 16 ? hda_rd16(reg) : hda_rd32(reg);
        if ((v & mask) == want)
            return true;
        pit_sleep_ms(1);
    }
    return false;
}

static void *hda_dma_alloc(uint64_t pages)
{
    uint8_t *p = (uint8_t *)pmm_alloc_pages(pages);
    if (!p)
        return NULL;
    memset(p, 0, pages * PAGE_SIZE);
    if (!hda.dma64 && virt_to_phys(p) + pages * PAGE_SIZE > 0x100000000ULL)
        kprintf("hda: warning, DMA buffer above 4 GiB on a 32-bit controller\n");
    return p;
}

static bool hda_command(uint8_t nid, uint32_t verb, uint32_t *resp)
{
    uint32_t cmd = ((uint32_t)hda.codec << 28) | ((uint32_t)nid << 20) | (verb & 0xFFFFF);

    hda_wr8(HDA_REG_RIRBSTS, 0x05);

    uint16_t wp = (uint16_t)((hda_rd16(HDA_REG_CORBWP) + 1) % hda.corb_entries);
    hda.corb[wp] = cmd;
    __asm__ volatile("" ::: "memory");
    hda_wr16(HDA_REG_CORBWP, wp);

    for (int spins = 0; spins < 2000000; spins++)
    {
        uint16_t rirb_wp = hda_rd16(HDA_REG_RIRBWP) & 0xFF;
        while (hda.rirb_rp != rirb_wp)
        {
            hda.rirb_rp = (uint16_t)((hda.rirb_rp + 1) % hda.rirb_entries);
            uint64_t entry = hda.rirb[hda.rirb_rp];
            if ((entry >> 32) & 0x10)
                continue;
            hda_wr8(HDA_REG_RIRBSTS, 0x05);
            if (resp)
                *resp = (uint32_t)entry;
            return true;
        }
        pause();
    }

    kprintf("hda: codec %d timed out on nid %d verb %x\n", hda.codec, nid, verb);
    return false;
}

static uint32_t hda_verb(uint8_t nid, uint32_t verb, uint32_t payload)
{
    uint32_t resp = 0;
    hda_command(nid, (verb << 8) | (payload & 0xFF), &resp);
    return resp;
}

static uint32_t hda_verb16(uint8_t nid, uint32_t verb, uint32_t payload)
{
    uint32_t resp = 0;
    hda_command(nid, (verb << 16) | (payload & 0xFFFF), &resp);
    return resp;
}

static uint32_t hda_param(uint8_t nid, uint8_t param)
{
    return hda_verb(nid, HDA_VERB_GET_PARAM, param);
}

static uint16_t hda_ring_size(uint32_t size_reg, uint16_t *entries)
{
    uint8_t cap = hda_rd8(size_reg) >> 4;
    if (cap & 0x4) { *entries = 256; return 0x2; }
    if (cap & 0x2) { *entries = 16;  return 0x1; }
    *entries = 2;
    return 0x0;
}

static bool hda_reset_controller(void)
{
    hda_wr8(HDA_REG_CORBCTL, 0);
    hda_wr8(HDA_REG_RIRBCTL, 0);
    hda_wait_bits(HDA_REG_CORBCTL, 0x2, 0, 8);
    hda_wait_bits(HDA_REG_RIRBCTL, 0x2, 0, 8);

    hda_wr32(HDA_REG_GCTL, hda_rd32(HDA_REG_GCTL) & ~HDA_GCTL_CRST);
    if (!hda_wait_bits(HDA_REG_GCTL, HDA_GCTL_CRST, 0, 32))
    {
        kprintf("hda: controller did not enter reset\n");
        return false;
    }
    pit_sleep_ms(1);

    hda_wr32(HDA_REG_GCTL, hda_rd32(HDA_REG_GCTL) | HDA_GCTL_CRST);
    if (!hda_wait_bits(HDA_REG_GCTL, HDA_GCTL_CRST, HDA_GCTL_CRST, 32))
    {
        kprintf("hda: controller did not leave reset\n");
        return false;
    }

    pit_sleep_ms(2);
    return true;
}

static bool hda_setup_rings(void)
{
    uint8_t *page = hda_dma_alloc(1);
    if (!page)
        return false;

    hda.corb = (volatile uint32_t *)page;
    hda.rirb = (volatile uint64_t *)(page + 1024);
    uint64_t corb_phys = virt_to_phys(page);
    uint64_t rirb_phys = corb_phys + 1024;

    hda_wr8(HDA_REG_CORBSIZE, (uint8_t)hda_ring_size(HDA_REG_CORBSIZE, &hda.corb_entries));
    hda_wr32(HDA_REG_CORBLBASE, (uint32_t)corb_phys);
    hda_wr32(HDA_REG_CORBUBASE, (uint32_t)(corb_phys >> 32));

    hda_wr16(HDA_REG_CORBRP, 0x8000);
    hda_wait_bits(HDA_REG_CORBRP, 0x8000, 0x8000, 16);
    hda_wr16(HDA_REG_CORBRP, 0);
    hda_wait_bits(HDA_REG_CORBRP, 0x8000, 0, 16);
    hda_wr16(HDA_REG_CORBWP, 0);

    hda_wr8(HDA_REG_RIRBSIZE, (uint8_t)hda_ring_size(HDA_REG_RIRBSIZE, &hda.rirb_entries));
    hda_wr32(HDA_REG_RIRBLBASE, (uint32_t)rirb_phys);
    hda_wr32(HDA_REG_RIRBUBASE, (uint32_t)(rirb_phys >> 32));
    hda_wr16(HDA_REG_RIRBWP, 0x8000);
    hda_wr16(HDA_REG_RINTCNT, 0xFF);
    hda.rirb_rp = 0;

    hda_wr8(HDA_REG_CORBCTL, 0x2);
    hda_wr8(HDA_REG_RIRBCTL, 0x2);
    hda_wait_bits(HDA_REG_CORBCTL, 0x2, 0x2, 8);
    hda_wait_bits(HDA_REG_RIRBCTL, 0x2, 0x2, 8);
    return true;
}

static void hda_read_connections(uint8_t nid, HdaWidget *w)
{
    w->conn_count = 0;
    if (!(w->caps & HDA_WCAP_CONN_LIST))
        return;

    uint32_t len_reg = hda_param(nid, HDA_PARAM_CONN_LEN);
    bool long_form = len_reg & 0x80;
    int len = (int)(len_reg & 0x7F);
    int per = long_form ? 2 : 4;
    int bits = long_form ? 16 : 8;
    uint32_t mask = long_form ? 0x7FFF : 0x7F;
    uint32_t range_bit = long_form ? 0x8000 : 0x80;

    uint32_t prev = 0;
    for (int i = 0; i < len; i++)
    {
        uint32_t resp = hda_verb(nid, HDA_VERB_GET_CONN_LIST, (uint32_t)(i - i % per));
        uint32_t entry = (resp >> ((i % per) * bits)) & ((1u << bits) - 1);
        uint32_t id = entry & mask;

        if ((entry & range_bit) && prev)
        {
            for (uint32_t n = prev + 1; n <= id && w->conn_count < HDA_MAX_CONN; n++)
                w->conn[w->conn_count++] = (uint8_t)n;
        }
        else if (w->conn_count < HDA_MAX_CONN && id < HDA_MAX_NODES)
        {
            w->conn[w->conn_count++] = (uint8_t)id;
        }
        prev = id;
    }
}

static bool hda_scan_afg(uint8_t afg)
{
    uint32_t count = hda_param(afg, HDA_PARAM_NODE_COUNT);
    uint32_t start = (count >> 16) & 0xFF;
    uint32_t total = count & 0xFF;
    if (total == 0)
        return false;

    hda.afg = afg;
    hda.afg_in_amp = hda_param(afg, HDA_PARAM_IN_AMP_CAP);
    hda.afg_out_amp = hda_param(afg, HDA_PARAM_OUT_AMP_CAP);
    hda_verb(afg, HDA_VERB_SET_POWER, 0);

    memset(hda.present, 0, sizeof(hda.present));
    for (uint32_t nid = start; nid < start + total && nid < HDA_MAX_NODES; nid++)
    {
        HdaWidget *w = &hda.widgets[nid];
        memset(w, 0, sizeof(*w));
        w->caps = hda_param((uint8_t)nid, HDA_PARAM_WIDGET_CAP);
        hda.present[nid] = true;

        if (w->caps & HDA_WCAP_AMP_OVERRIDE)
        {
            w->in_amp = hda_param((uint8_t)nid, HDA_PARAM_IN_AMP_CAP);
            w->out_amp = hda_param((uint8_t)nid, HDA_PARAM_OUT_AMP_CAP);
        }
        else
        {
            w->in_amp = hda.afg_in_amp;
            w->out_amp = hda.afg_out_amp;
        }

        if (HDA_WCAP_TYPE(w->caps) == HDA_WIDGET_PIN)
        {
            w->pin_caps = hda_param((uint8_t)nid, HDA_PARAM_PIN_CAP);
            w->config = hda_verb((uint8_t)nid, HDA_VERB_GET_CONFIG, 0);
        }

        hda_read_connections((uint8_t)nid, w);
    }
    return true;
}

static int hda_type(uint8_t nid)
{
    return hda.present[nid] ? (int)HDA_WCAP_TYPE(hda.widgets[nid].caps) : -1;
}

static bool hda_is_digital(uint8_t nid)
{
    return hda.widgets[nid].caps & HDA_WCAP_DIGITAL;
}

typedef bool (*HdaPathGoal)(uint8_t nid, int want);

static bool hda_find_path(uint8_t nid, HdaPathGoal goal, int want, HdaPath *path)
{
    if (path->len >= HDA_MAX_PATH)
        return false;
    for (int i = 0; i < path->len; i++)
        if (path->nid[i] == nid)
            return false;

    int at = path->len++;
    path->nid[at] = nid;

    if (at > 0 && goal(nid, want))
        return true;

    int type = hda_type(nid);
    if (at == 0 || type == HDA_WIDGET_MIXER || type == HDA_WIDGET_SELECTOR)
    {
        HdaWidget *w = &hda.widgets[nid];
        for (int i = 0; i < w->conn_count; i++)
        {
            path->sel[at] = (uint8_t)i;
            if (hda_find_path(w->conn[i], goal, want, path))
                return true;
        }
    }

    path->len--;
    return false;
}

static bool hda_goal_dac(uint8_t nid, int want)
{
    (void)want;
    return hda_type(nid) == HDA_WIDGET_OUTPUT && !hda_is_digital(nid);
}

static bool hda_goal_input_pin(uint8_t nid, int want)
{
    if (hda_type(nid) != HDA_WIDGET_PIN || hda_is_digital(nid))
        return false;
    HdaWidget *w = &hda.widgets[nid];
    return (w->pin_caps & HDA_PINCAP_IN) &&
           HDA_CFG_CONN(w->config) != HDA_CONN_NONE &&
           (int)HDA_CFG_DEVICE(w->config) == want;
}

static uint32_t hda_amp_gain(uint32_t amp_caps)
{
    uint32_t offset = amp_caps & 0x7F;
    uint32_t steps = (amp_caps >> 8) & 0x7F;
    return MIN(offset, steps);
}

static void hda_unmute_out(uint8_t nid)
{
    HdaWidget *w = &hda.widgets[nid];
    if (w->caps & HDA_WCAP_OUT_AMP)
        hda_verb16(nid, HDA_VERB_SET_AMP, HDA_AMP_SET_OUT | HDA_AMP_SET_LEFT | HDA_AMP_SET_RIGHT |
                                          hda_amp_gain(w->out_amp));
}

static void hda_unmute_in(uint8_t nid, uint8_t index)
{
    HdaWidget *w = &hda.widgets[nid];
    if (w->caps & HDA_WCAP_IN_AMP)
        hda_verb16(nid, HDA_VERB_SET_AMP, HDA_AMP_SET_IN | HDA_AMP_SET_LEFT | HDA_AMP_SET_RIGHT |
                                          ((uint32_t)index << 8) | hda_amp_gain(w->in_amp));
}

static void hda_enable_path(const HdaPath *path)
{
    for (int i = 0; i < path->len; i++)
    {
        uint8_t nid = path->nid[i];
        HdaWidget *w = &hda.widgets[nid];

        hda_verb(nid, HDA_VERB_SET_POWER, 0);
        hda_unmute_out(nid);

        if (i + 1 < path->len)
        {
            if (hda_type(nid) != HDA_WIDGET_MIXER && w->conn_count > 1)
                hda_verb(nid, HDA_VERB_SET_CONN_SEL, path->sel[i]);
            hda_unmute_in(nid, path->sel[i]);
        }
        else
        {
            hda_unmute_in(nid, 0);
        }
    }
}

static void hda_setup_converter(uint8_t nid, uint8_t tag)
{
    hda_verb16(nid, HDA_VERB_SET_FORMAT, HDA_FORMAT);
    hda_verb(nid, HDA_VERB_SET_STREAM, (uint32_t)tag << 4);
}

static bool hda_setup_output(void)
{
    static const int wanted[] = { HDA_DEV_SPEAKER, HDA_DEV_HP_OUT, HDA_DEV_LINE_OUT };
    int routed = 0;

    for (int d = 0; d < (int)ARRAY_LEN(wanted); d++)
    {
        for (int nid = 0; nid < HDA_MAX_NODES && routed < HDA_MAX_OUT_PINS; nid++)
        {
            if (hda_type((uint8_t)nid) != HDA_WIDGET_PIN || hda_is_digital((uint8_t)nid))
                continue;
            HdaWidget *w = &hda.widgets[nid];
            if (!(w->pin_caps & HDA_PINCAP_OUT) || HDA_CFG_CONN(w->config) == HDA_CONN_NONE ||
                (int)HDA_CFG_DEVICE(w->config) != wanted[d])
                continue;

            HdaPath path = { 0 };
            if (!hda_find_path((uint8_t)nid, hda_goal_dac, 0, &path))
                continue;

            hda_enable_path(&path);

            uint8_t ctl = HDA_PINCTL_OUT;
            if (wanted[d] == HDA_DEV_HP_OUT && (w->pin_caps & HDA_PINCAP_HP_DRIVE))
                ctl |= HDA_PINCTL_HP;
            hda_verb((uint8_t)nid, HDA_VERB_SET_PIN_CTL, ctl);
            if (w->pin_caps & HDA_PINCAP_EAPD)
                hda_verb((uint8_t)nid, HDA_VERB_SET_EAPD, 0x2);

            uint8_t dac = path.nid[path.len - 1];
            hda_setup_converter(dac, HDA_STREAM_TAG_OUT);

            kprintf("hda: output pin %d (device %d) via DAC %d\n", nid, wanted[d], dac);
            routed++;
        }
    }
    return routed > 0;
}

static bool hda_setup_input(void)
{
    static const int wanted[] = { HDA_DEV_MIC_IN, HDA_DEV_LINE_IN };

    for (int d = 0; d < (int)ARRAY_LEN(wanted); d++)
    {
        for (int nid = 0; nid < HDA_MAX_NODES; nid++)
        {
            if (hda_type((uint8_t)nid) != HDA_WIDGET_INPUT || hda_is_digital((uint8_t)nid))
                continue;

            HdaPath path = { 0 };
            if (!hda_find_path((uint8_t)nid, hda_goal_input_pin, wanted[d], &path))
                continue;

            hda_enable_path(&path);

            uint8_t pin = path.nid[path.len - 1];
            HdaWidget *pw = &hda.widgets[pin];
            uint8_t ctl = HDA_PINCTL_IN;
            if (wanted[d] == HDA_DEV_MIC_IN)
            {
                if (pw->pin_caps & HDA_PINCAP_VREF_80)
                    ctl |= HDA_PINCTL_VREF_80;
                else if (pw->pin_caps & HDA_PINCAP_VREF_50)
                    ctl |= HDA_PINCTL_VREF_50;
            }
            hda_verb(pin, HDA_VERB_SET_PIN_CTL, ctl);
            hda_setup_converter((uint8_t)nid, HDA_STREAM_TAG_IN);

            kprintf("hda: input pin %d (device %d) via ADC %d\n", pin, wanted[d], nid);
            return true;
        }
    }
    return false;
}

static bool hda_init_codec(uint8_t codec)
{
    hda.codec = codec;

    uint32_t vendor = hda_param(0, HDA_PARAM_VENDOR);
    uint32_t count = hda_param(0, HDA_PARAM_NODE_COUNT);
    uint32_t start = (count >> 16) & 0xFF;
    uint32_t total = count & 0xFF;

    kprintf("hda: codec %d vendor %x:%x\n", codec, vendor >> 16, vendor & 0xFFFF);

    for (uint32_t fg = start; fg < start + total; fg++)
    {
        if ((hda_param((uint8_t)fg, HDA_PARAM_FG_TYPE) & 0xFF) != HDA_FG_AUDIO)
            continue;
        if (!hda_scan_afg((uint8_t)fg))
            continue;

        hda.can_play = hda_setup_output();
        hda.can_capture = hda_setup_input();
        if (hda.can_play || hda.can_capture)
            return true;
    }
    return false;
}

static bool hda_stream_alloc(HdaStream *s, int index, uint8_t tag)
{
    s->regs = hda.mmio + HDA_REG_SD_BASE + index * HDA_SD_STRIDE;
    s->tag = tag;
    s->buf = hda_dma_alloc(HDA_BUF_PAGES);
    s->bdl = hda_dma_alloc(1);
    if (!s->buf || !s->bdl)
        return false;

    for (int i = 0; i < HDA_PERIODS; i++)
    {
        s->bdl[i].addr = virt_to_phys(s->buf) + (uint64_t)i * HDA_PERIOD;
        s->bdl[i].len = HDA_PERIOD;
        s->bdl[i].flags = 0;
    }
    return true;
}

static void hda_stream_reset(HdaStream *s)
{
    sd_wr32(s, HDA_SD_CTL, sd_rd32(s, HDA_SD_CTL) & ~HDA_SD_CTL_RUN & 0x00FFFFFF);
    for (int i = 0; i < 1000 && (sd_rd32(s, HDA_SD_CTL) & HDA_SD_CTL_RUN); i++)
        pause();

    sd_wr32(s, HDA_SD_CTL, (sd_rd32(s, HDA_SD_CTL) & 0x00FFFFFF) | HDA_SD_CTL_SRST);
    for (int i = 0; i < 100000 && !(sd_rd32(s, HDA_SD_CTL) & HDA_SD_CTL_SRST); i++)
        pause();
    sd_wr32(s, HDA_SD_CTL, sd_rd32(s, HDA_SD_CTL) & 0x00FFFFFF & ~HDA_SD_CTL_SRST);
    for (int i = 0; i < 100000 && (sd_rd32(s, HDA_SD_CTL) & HDA_SD_CTL_SRST); i++)
        pause();

    sd_wr8(s, HDA_SD_STS, HDA_SD_STS_CLEAR);
}

static void hda_stream_start(HdaStream *s)
{
    hda_stream_reset(s);

    uint64_t bdl_phys = virt_to_phys(s->bdl);
    sd_wr32(s, HDA_SD_BDPL, (uint32_t)bdl_phys);
    sd_wr32(s, HDA_SD_BDPU, (uint32_t)(bdl_phys >> 32));
    sd_wr32(s, HDA_SD_CBL, HDA_BUF_SIZE);
    sd_wr16(s, HDA_SD_LVI, HDA_PERIODS - 1);
    sd_wr16(s, HDA_SD_FMT, HDA_FORMAT);

    uint32_t ctl = sd_rd32(s, HDA_SD_CTL) & 0x000FFFFF;
    sd_wr32(s, HDA_SD_CTL, ctl | ((uint32_t)s->tag << 20));

    s->last_lpib = 0;
    s->pos = 0;

    sd_wr32(s, HDA_SD_CTL, (sd_rd32(s, HDA_SD_CTL) & 0x00FFFFFF) | HDA_SD_CTL_RUN);
    s->running = true;
}

static void hda_stream_stop(HdaStream *s)
{
    sd_wr32(s, HDA_SD_CTL, sd_rd32(s, HDA_SD_CTL) & 0x00FFFFFF & ~HDA_SD_CTL_RUN);
    sd_wr8(s, HDA_SD_STS, HDA_SD_STS_CLEAR);
    s->running = false;
}

static void hda_stream_update(HdaStream *s)
{
    if (!s->running)
        return;
    uint32_t lpib = sd_rd32(s, HDA_SD_LPIB);
    if (lpib >= HDA_BUF_SIZE)
        return;
    s->pos += (lpib + HDA_BUF_SIZE - s->last_lpib) % HDA_BUF_SIZE;
    s->last_lpib = lpib;
}

static void hda_ring_zero(uint8_t *ring, uint64_t from, uint64_t to)
{
    if (to - from >= HDA_BUF_SIZE)
    {
        memset(ring, 0, HDA_BUF_SIZE);
        return;
    }
    while (from < to)
    {
        uint32_t off = (uint32_t)(from % HDA_BUF_SIZE);
        uint32_t n = (uint32_t)MIN(to - from, (uint64_t)(HDA_BUF_SIZE - off));
        memset(ring + off, 0, n);
        from += n;
    }
}

static void hda_mix_client(HdaPlayClient *c, uint64_t at, uint32_t bytes)
{
    uint32_t avail = (uint32_t)(c->head - c->tail) & ~(uint32_t)(HDA_FRAME - 1);
    bytes = MIN(bytes, avail);
    int32_t vol = (int32_t)hda.volume;

    for (uint32_t i = 0; i < bytes; i += 2)
    {
        int16_t *dst = (int16_t *)(hda.out.buf + (at + i) % HDA_BUF_SIZE);
        int16_t src = *(int16_t *)(c->ring + (c->tail + i) % HDA_CLIENT_RING);
        int32_t v = *dst + ((int32_t)src * vol >> 8);
        if (v > 32767)
            v = 32767;
        else if (v < -32768)
            v = -32768;
        *dst = (int16_t)v;
    }
    c->tail += bytes;
}

static void hda_play_fill(void)
{
    uint64_t target = (hda.out.pos + HDA_PLAY_LEAD) & ~(uint64_t)(HDA_FRAME - 1);
    if (hda.fill < hda.out.pos)
        hda.fill = ALIGN_UP(hda.out.pos, HDA_FRAME);
    if (target <= hda.fill)
        return;

    uint32_t bytes = (uint32_t)(target - hda.fill);
    for (HdaPlayClient *c = hda.play_clients; c; c = c->next)
        hda_mix_client(c, hda.fill, bytes);
    hda.fill = target;
}

static void hda_play_start(void)
{
    if (hda.out.running)
        return;
    memset(hda.out.buf, 0, HDA_BUF_SIZE);
    hda.fill = 0;
    hda.out.pos = 0;
    hda_play_fill();
    hda_stream_start(&hda.out);
}

static void hda_play_poll(uint64_t now)
{
    HdaPlayClient **link = &hda.play_clients;
    while (*link)
    {
        HdaPlayClient *c = *link;
        if (c->closing && c->head == c->tail)
        {
            *link = c->next;
            kfree(c);
            continue;
        }
        link = &c->next;
    }

    if (!hda.out.running)
        return;

    uint64_t old = hda.out.pos;
    hda_stream_update(&hda.out);
    hda_ring_zero(hda.out.buf, old, hda.out.pos);
    hda_play_fill();

    if (hda.play_clients)
        hda.idle_since = now;
    else if (now - hda.idle_since > HDA_IDLE_STOP_MS)
        hda_stream_stop(&hda.out);
}

static void hda_capture_trim(HdaCaptureClient *c)
{
    if (hda.in.pos - c->pos > HDA_CAPTURE_KEEP)
        c->pos = hda.in.pos - HDA_CAPTURE_KEEP;
}

static HdaCaptureClient *hda_capture_open(void)
{
    HdaCaptureClient *c = kmalloc(sizeof(*c));
    if (!c)
        return NULL;

    uint64_t flags = irq_save();
    if (!hda.in.running)
        hda_stream_start(&hda.in);
    hda_stream_update(&hda.in);
    c->pos = hda.in.pos;
    c->next = hda.capture_clients;
    hda.capture_clients = c;
    irq_restore(flags);
    return c;
}

static void hda_capture_close(HdaCaptureClient *c)
{
    uint64_t flags = irq_save();
    for (HdaCaptureClient **link = &hda.capture_clients; *link; link = &(*link)->next)
    {
        if (*link == c)
        {
            *link = c->next;
            break;
        }
    }
    if (!hda.capture_clients && hda.in.running)
        hda_stream_stop(&hda.in);
    irq_restore(flags);
    kfree(c);
}

void hda_poll(void)
{
    if (!hda.ready)
        return;

    uint64_t out_before = hda.out.pos / HDA_PERIOD;
    uint64_t in_before = hda.in.pos / HDA_PERIOD;

    hda_play_poll(task_uptime_ms());
    hda_stream_update(&hda.in);

    if (hda.out.pos / HDA_PERIOD != out_before || hda.in.pos / HDA_PERIOD != in_before)
        task_wake_waiters();
}

static HdaFile *hda_file(VFile *file)
{
    return (HdaFile *)file->private_data;
}

static int hda_open(VNode *node, VFile *file)
{
    (void)node;
    if (!hda.ready)
        return -1;

    HdaFile *f = kmalloc(sizeof(HdaFile));
    if (!f)
        return -1;
    memset(f, 0, sizeof(*f));

    if ((file->flags & 0x3) != VFS_O_WRONLY)
    {
        if (!hda.can_capture || !(f->capture = hda_capture_open()))
        {
            kfree(f);
            return -1;
        }
    }

    file->private_data = f;
    return 0;
}

static int hda_close(VFile *file)
{
    HdaFile *f = hda_file(file);
    if (!f)
        return 0;

    if (f->capture)
        hda_capture_close(f->capture);
    if (f->play)
    {
        uint64_t flags = irq_save();
        f->play->closing = true;
        irq_restore(flags);
    }
    kfree(f);
    file->private_data = NULL;
    return 0;
}

static ssize_t hda_write(VFile *file, const void *buf, size_t count)
{
    HdaFile *f = hda_file(file);
    if (!f || !hda.can_play || (file->flags & 0x3) == VFS_O_RDONLY)
        return -1;

    if (!f->play)
    {
        HdaPlayClient *c = kmalloc(sizeof(HdaPlayClient));
        if (!c)
            return -1;
        memset(c, 0, sizeof(*c));

        uint64_t flags = irq_save();
        c->next = hda.play_clients;
        hda.play_clients = c;
        irq_restore(flags);
        f->play = c;
    }

    HdaPlayClient *c = f->play;
    const uint8_t *src = (const uint8_t *)buf;
    size_t done = 0;

    while (done < count)
    {
        uint64_t flags = irq_save();

        hda_play_start();
        hda.idle_since = task_uptime_ms();

        size_t space = HDA_CLIENT_RING - (size_t)(c->head - c->tail);
        if (space)
        {
            size_t n = MIN(space, count - done);
            size_t off = (size_t)(c->head % HDA_CLIENT_RING);
            size_t first = MIN(n, HDA_CLIENT_RING - off);
            memcpy(c->ring + off, src + done, first);
            memcpy(c->ring, src + done + first, n - first);
            c->head += n;
            done += n;
            irq_restore(flags);
            continue;
        }

        if (task_current()->kill_pending)
        {
            irq_restore(flags);
            break;
        }
        task_block(TASK_BLOCK_FOREVER);
        irq_restore(flags);
    }
    return (ssize_t)done;
}

static ssize_t hda_read(VFile *file, void *buf, size_t count)
{
    HdaFile *f = hda_file(file);
    if (!f || !f->capture)
        return -1;

    HdaCaptureClient *c = f->capture;
    count &= ~(size_t)(HDA_FRAME - 1);
    count = MIN(count, (size_t)HDA_BUF_SIZE / 2);
    if (count == 0)
        return 0;

    for (;;)
    {
        uint64_t flags = irq_save();
        hda_stream_update(&hda.in);
        hda_capture_trim(c);

        if (hda.in.pos - c->pos >= count)
        {
            uint8_t *dst = (uint8_t *)buf;
            size_t off = (size_t)(c->pos % HDA_BUF_SIZE);
            size_t first = MIN(count, HDA_BUF_SIZE - off);
            memcpy(dst, hda.in.buf + off, first);
            memcpy(dst + first, hda.in.buf, count - first);
            c->pos += count;
            irq_restore(flags);
            return (ssize_t)count;
        }

        if (task_current()->kill_pending)
        {
            irq_restore(flags);
            return -1;
        }
        task_block(TASK_BLOCK_FOREVER);
        irq_restore(flags);
    }
}

static int hda_poll_file(VFile *file)
{
    HdaFile *f = hda_file(file);
    if (!f)
        return POLLERR;

    int events = 0;
    uint64_t flags = irq_save();

    if (hda.can_play && (file->flags & 0x3) != VFS_O_RDONLY)
    {
        if (!f->play || HDA_CLIENT_RING - (f->play->head - f->play->tail) >= HDA_PERIOD)
            events |= POLLOUT;
    }

    if (f->capture)
    {
        hda_stream_update(&hda.in);
        hda_capture_trim(f->capture);
        if (hda.in.pos - f->capture->pos >= HDA_PERIOD)
            events |= POLLIN;
    }

    irq_restore(flags);
    return events;
}

static int hda_ioctl(VFile *file, uint64_t request, void *arg)
{
    HdaFile *f = hda_file(file);
    if (!f)
        return -1;

    switch (request)
    {
        case DEVFS_IOCTL_AUDIO_GET_INFO:
        {
            if (!arg)
                return -1;
            AudioInfo info = {
                .sample_rate  = HDA_RATE,
                .channels     = HDA_CHANNELS,
                .bits         = 16,
                .playback     = hda.can_play,
                .capture      = hda.can_capture,
                .period_bytes = HDA_PERIOD,
                .queue_bytes  = HDA_CLIENT_RING,
                .latency_bytes = HDA_PLAY_LEAD,
            };
            memcpy(arg, &info, sizeof(info));
            return 0;
        }
        case DEVFS_IOCTL_AUDIO_GET_STATE:
        {
            if (!arg)
                return -1;
            AudioState state = { 0 };
            uint64_t flags = irq_save();
            if (f->play)
                state.play_queued = (uint32_t)(f->play->head - f->play->tail);
            state.play_free = HDA_CLIENT_RING - state.play_queued;
            if (f->capture)
            {
                hda_stream_update(&hda.in);
                hda_capture_trim(f->capture);
                state.capture_avail = (uint32_t)(hda.in.pos - f->capture->pos);
            }
            irq_restore(flags);
            memcpy(arg, &state, sizeof(state));
            return 0;
        }
        case DEVFS_IOCTL_AUDIO_SET_VOLUME:
        {
            if (!arg)
                return -1;
            uint32_t v = MIN(*(uint32_t *)arg, 100u);
            hda.volume = v * 256 / 100;
            return 0;
        }
        case DEVFS_IOCTL_AUDIO_GET_VOLUME:
        {
            if (!arg)
                return -1;
            *(uint32_t *)arg = (hda.volume * 100 + 128) / 256;
            return 0;
        }
        case DEVFS_IOCTL_AUDIO_DROP:
        {
            uint64_t flags = irq_save();
            if (f->play)
                f->play->tail = f->play->head;
            if (f->capture)
            {
                hda_stream_update(&hda.in);
                f->capture->pos = hda.in.pos;
            }
            irq_restore(flags);
            return 0;
        }
        default:
            return -1;
    }
}

typedef struct
{
    PciDevice dev;
    bool found;
}
HdaFindCtx;

static void hda_find_cb(const PciDevice *dev, void *ctx_)
{
    HdaFindCtx *ctx = ctx_;
    if (!ctx->found && dev->class_code == 0x04 && dev->subclass == 0x03)
    {
        ctx->dev = *dev;
        ctx->found = true;
    }
}

int hda_init(void)
{
    HdaFindCtx ctx = { 0 };
    pci_enumerate(hda_find_cb, &ctx);
    if (!ctx.found)
    {
        kprintf("hda: no HD Audio controller found\n");
        return 0;
    }

    PciDevice *dev = &ctx.dev;
    pci_enable_device(dev);

    if (pci_bar_is_io(dev->bar[0]))
    {
        kprintf("hda: BAR0 is not MMIO\n");
        return 0;
    }

    memset(&hda, 0, sizeof(hda));
    hda.volume = 256;
    hda.mmio = (volatile uint8_t *)vmm_map_mmio(pci_bar_addr(dev, 0), 0x4000);

    uint16_t gcap = hda_rd16(HDA_REG_GCAP);
    int iss = (gcap >> 8) & 0xF;
    int oss = (gcap >> 12) & 0xF;
    hda.dma64 = gcap & 1;

    kprintf("hda: controller %x:%x v%d.%d, %d in / %d out streams\n",
            dev->vendor_id, dev->device_id, hda_rd8(HDA_REG_VMAJ), hda_rd8(HDA_REG_VMIN), iss, oss);

    if (!hda_reset_controller())
        return 0;

    hda_wr32(HDA_REG_INTCTL, 0);
    hda_wr32(HDA_REG_DPLBASE, 0);
    hda_wr32(HDA_REG_DPUBASE, 0);

    if (!hda_setup_rings())
    {
        kprintf("hda: failed to allocate command rings\n");
        return 0;
    }

    uint16_t codecs = hda_rd16(HDA_REG_STATESTS);
    hda_wr16(HDA_REG_STATESTS, codecs);

    bool have_codec = false;
    for (uint8_t cad = 0; cad < 15 && !have_codec; cad++)
        if (codecs & (1u << cad))
            have_codec = hda_init_codec(cad);

    if (!have_codec)
    {
        kprintf("hda: no usable codec (STATESTS %x)\n", codecs);
        return 0;
    }

    if (hda.can_capture && (iss == 0 || !hda_stream_alloc(&hda.in, 0, HDA_STREAM_TAG_IN)))
        hda.can_capture = false;
    if (hda.can_play && (oss == 0 || !hda_stream_alloc(&hda.out, iss, HDA_STREAM_TAG_OUT)))
        hda.can_play = false;

    if (!hda.can_play && !hda.can_capture)
        return 0;

    static VFileOps file_ops;
    file_ops.read  = hda_read;
    file_ops.write = hda_write;
    file_ops.ioctl = hda_ioctl;
    file_ops.close = hda_close;
    file_ops.poll  = hda_poll_file;

    static VNodeOps node_ops;
    node_ops.open = hda_open;

    VNode *node = devfs_register("audio", &file_ops, NULL);
    if (!node)
        return 0;
    node->ops = &node_ops;

    hda.ready = true;
    kprintf("hda: /dev/audio ready (%s%s%s)\n", hda.can_play ? "playback" : "",
            hda.can_play && hda.can_capture ? ", " : "", hda.can_capture ? "capture" : "");
    return 1;
}

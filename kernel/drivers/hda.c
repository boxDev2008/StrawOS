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

#define HDA_CFG_LOCATION(c)     (((c) >> 24) & 0x3F)
#define HDA_CONN_FIXED          0x2

#define HDA_MAX_NODES     256
#define HDA_MAX_CONN      32
#define HDA_MAX_PATH      6
#define HDA_MAX_CTLS      4
#define HDA_MAX_CODECS    8

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

#define HDA_UNITY         256

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
    volatile uint32_t *dpos;
    uint8_t     *buf;
    HdaBdlEntry *bdl;
    uint8_t      tag;
    bool         running;
    uint32_t     last_lpib;
    uint64_t     pos;
}
HdaStream;

typedef struct
{
    volatile uint8_t  *mmio;
    bool               dma64;
    volatile uint32_t *corb;
    volatile uint64_t *rirb;
    uint16_t           corb_entries;
    uint16_t           rirb_entries;
    uint16_t           rirb_rp;
    int                iss;
    int                oss;
    volatile uint32_t *dpos;
    PciDevice          pci;
    HdaStream          out;
    HdaStream          in;
    bool               out_ok;
    bool               in_ok;
}
HdaCtl;

typedef struct
{
    HdaCtl   *ctl;
    uint8_t   cad;
    uint8_t   afg;
    uint32_t  afg_in_amp;
    uint32_t  afg_out_amp;
    HdaWidget widgets[HDA_MAX_NODES];
    bool      present[HDA_MAX_NODES];
}
HdaCodec;

typedef struct
{
    HdaCodec *codec;
    bool      input;
    uint8_t   pin;
    uint8_t   conv;
    uint8_t   type;
    HdaPath   path;
    char      name[48];
    char      key[40];
}
HdaDev;

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

static HdaCtl   hda_ctls[HDA_MAX_CTLS];
static HdaCodec hda_codecs[HDA_MAX_CODECS];

static struct
{
    bool       ready;
    int        n_ctls;
    int        n_codecs;
    HdaDev     devs[AUDIO_MAX_DEVICES];
    int        n_devs;
    int        out_dev;
    int        in_dev;

    HdaStream *out;
    HdaStream *in;

    uint64_t   fill;
    uint64_t   idle_since;
    uint32_t   volume;
    uint32_t   in_volume;

    HdaPlayClient    *play_clients;
    HdaCaptureClient *capture_clients;
}
hda;

static uint8_t  hda_rd8(HdaCtl *c, uint32_t reg)  { return *(volatile uint8_t  *)(c->mmio + reg); }
static uint16_t hda_rd16(HdaCtl *c, uint32_t reg) { return *(volatile uint16_t *)(c->mmio + reg); }
static uint32_t hda_rd32(HdaCtl *c, uint32_t reg) { return *(volatile uint32_t *)(c->mmio + reg); }
static void hda_wr8(HdaCtl *c, uint32_t reg, uint8_t v)   { *(volatile uint8_t  *)(c->mmio + reg) = v; }
static void hda_wr16(HdaCtl *c, uint32_t reg, uint16_t v) { *(volatile uint16_t *)(c->mmio + reg) = v; }
static void hda_wr32(HdaCtl *c, uint32_t reg, uint32_t v) { *(volatile uint32_t *)(c->mmio + reg) = v; }

static uint32_t sd_rd32(HdaStream *s, uint32_t reg) { return *(volatile uint32_t *)(s->regs + reg); }
static void sd_wr8(HdaStream *s, uint32_t reg, uint8_t v)   { *(volatile uint8_t  *)(s->regs + reg) = v; }
static void sd_wr16(HdaStream *s, uint32_t reg, uint16_t v) { *(volatile uint16_t *)(s->regs + reg) = v; }
static void sd_wr32(HdaStream *s, uint32_t reg, uint32_t v) { *(volatile uint32_t *)(s->regs + reg) = v; }

static bool hda_wait_bits(HdaCtl *c, uint32_t reg, uint32_t mask, uint32_t want, int width)
{
    for (int i = 0; i < 1000; i++)
    {
        uint32_t v = width == 8 ? hda_rd8(c, reg) : width == 16 ? hda_rd16(c, reg) : hda_rd32(c, reg);
        if ((v & mask) == want)
            return true;
        pit_sleep_ms(1);
    }
    return false;
}

static void *hda_dma_alloc(HdaCtl *c, uint64_t pages)
{
    uint8_t *p = (uint8_t *)pmm_alloc_pages(pages);
    if (!p)
        return NULL;
    memset(p, 0, pages * PAGE_SIZE);
    if (!c->dma64 && virt_to_phys(p) + pages * PAGE_SIZE > 0x100000000ULL)
        kprintf("hda: warning, DMA buffer above 4 GiB on a 32-bit controller\n");
    return p;
}

static bool hda_command(HdaCodec *codec, uint8_t nid, uint32_t verb, uint32_t *resp)
{
    HdaCtl *c = codec->ctl;
    uint32_t cmd = ((uint32_t)codec->cad << 28) | ((uint32_t)nid << 20) | (verb & 0xFFFFF);

    hda_wr8(c, HDA_REG_RIRBSTS, 0x05);

    uint16_t wp = (uint16_t)((hda_rd16(c, HDA_REG_CORBWP) + 1) % c->corb_entries);
    c->corb[wp] = cmd;
    __asm__ volatile("" ::: "memory");
    hda_wr16(c, HDA_REG_CORBWP, wp);

    for (int spins = 0; spins < 2000000; spins++)
    {
        uint16_t rirb_wp = hda_rd16(c, HDA_REG_RIRBWP) & 0xFF;
        while (c->rirb_rp != rirb_wp)
        {
            c->rirb_rp = (uint16_t)((c->rirb_rp + 1) % c->rirb_entries);
            uint64_t entry = c->rirb[c->rirb_rp];
            if ((entry >> 32) & 0x10)
                continue;
            hda_wr8(c, HDA_REG_RIRBSTS, 0x05);
            if (resp)
                *resp = (uint32_t)entry;
            return true;
        }
        pause();
    }

    kprintf("hda: codec %d timed out on nid %d verb %x\n", codec->cad, nid, verb);
    return false;
}

static uint32_t hda_verb(HdaCodec *c, uint8_t nid, uint32_t verb, uint32_t payload)
{
    uint32_t resp = 0;
    hda_command(c, nid, (verb << 8) | (payload & 0xFF), &resp);
    return resp;
}

static uint32_t hda_verb16(HdaCodec *c, uint8_t nid, uint32_t verb, uint32_t payload)
{
    uint32_t resp = 0;
    hda_command(c, nid, (verb << 16) | (payload & 0xFFFF), &resp);
    return resp;
}

static uint32_t hda_param(HdaCodec *c, uint8_t nid, uint8_t param)
{
    return hda_verb(c, nid, HDA_VERB_GET_PARAM, param);
}

static uint16_t hda_ring_size(HdaCtl *c, uint32_t size_reg, uint16_t *entries)
{
    uint8_t cap = hda_rd8(c, size_reg) >> 4;
    if (cap & 0x4) { *entries = 256; return 0x2; }
    if (cap & 0x2) { *entries = 16;  return 0x1; }
    *entries = 2;
    return 0x0;
}

static bool hda_reset_controller(HdaCtl *c)
{
    hda_wr8(c, HDA_REG_CORBCTL, 0);
    hda_wr8(c, HDA_REG_RIRBCTL, 0);
    hda_wait_bits(c, HDA_REG_CORBCTL, 0x2, 0, 8);
    hda_wait_bits(c, HDA_REG_RIRBCTL, 0x2, 0, 8);

    hda_wr32(c, HDA_REG_GCTL, hda_rd32(c, HDA_REG_GCTL) & ~HDA_GCTL_CRST);
    if (!hda_wait_bits(c, HDA_REG_GCTL, HDA_GCTL_CRST, 0, 32))
    {
        kprintf("hda: controller did not enter reset\n");
        return false;
    }
    pit_sleep_ms(1);

    hda_wr32(c, HDA_REG_GCTL, hda_rd32(c, HDA_REG_GCTL) | HDA_GCTL_CRST);
    if (!hda_wait_bits(c, HDA_REG_GCTL, HDA_GCTL_CRST, HDA_GCTL_CRST, 32))
    {
        kprintf("hda: controller did not leave reset\n");
        return false;
    }

    pit_sleep_ms(2);
    return true;
}

static bool hda_setup_rings(HdaCtl *c)
{
    uint8_t *page = hda_dma_alloc(c, 1);
    if (!page)
        return false;

    c->corb = (volatile uint32_t *)page;
    c->rirb = (volatile uint64_t *)(page + 1024);
    uint64_t corb_phys = virt_to_phys(page);
    uint64_t rirb_phys = corb_phys + 1024;

    hda_wr8(c, HDA_REG_CORBSIZE, (uint8_t)hda_ring_size(c, HDA_REG_CORBSIZE, &c->corb_entries));
    hda_wr32(c, HDA_REG_CORBLBASE, (uint32_t)corb_phys);
    hda_wr32(c, HDA_REG_CORBUBASE, (uint32_t)(corb_phys >> 32));

    hda_wr16(c, HDA_REG_CORBRP, 0x8000);
    hda_wait_bits(c, HDA_REG_CORBRP, 0x8000, 0x8000, 16);
    hda_wr16(c, HDA_REG_CORBRP, 0);
    hda_wait_bits(c, HDA_REG_CORBRP, 0x8000, 0, 16);
    hda_wr16(c, HDA_REG_CORBWP, 0);

    hda_wr8(c, HDA_REG_RIRBSIZE, (uint8_t)hda_ring_size(c, HDA_REG_RIRBSIZE, &c->rirb_entries));
    hda_wr32(c, HDA_REG_RIRBLBASE, (uint32_t)rirb_phys);
    hda_wr32(c, HDA_REG_RIRBUBASE, (uint32_t)(rirb_phys >> 32));
    hda_wr16(c, HDA_REG_RIRBWP, 0x8000);
    hda_wr16(c, HDA_REG_RINTCNT, 0xFF);
    c->rirb_rp = 0;

    hda_wr8(c, HDA_REG_CORBCTL, 0x2);
    hda_wr8(c, HDA_REG_RIRBCTL, 0x2);
    hda_wait_bits(c, HDA_REG_CORBCTL, 0x2, 0x2, 8);
    hda_wait_bits(c, HDA_REG_RIRBCTL, 0x2, 0x2, 8);
    return true;
}

static void hda_read_connections(HdaCodec *c, uint8_t nid, HdaWidget *w)
{
    w->conn_count = 0;
    if (!(w->caps & HDA_WCAP_CONN_LIST))
        return;

    uint32_t len_reg = hda_param(c, nid, HDA_PARAM_CONN_LEN);
    bool long_form = len_reg & 0x80;
    int len = (int)(len_reg & 0x7F);
    int per = long_form ? 2 : 4;
    int bits = long_form ? 16 : 8;
    uint32_t mask = long_form ? 0x7FFF : 0x7F;
    uint32_t range_bit = long_form ? 0x8000 : 0x80;

    uint32_t prev = 0;
    for (int i = 0; i < len; i++)
    {
        uint32_t resp = hda_verb(c, nid, HDA_VERB_GET_CONN_LIST, (uint32_t)(i - i % per));
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

static bool hda_scan_afg(HdaCodec *c, uint8_t afg)
{
    uint32_t count = hda_param(c, afg, HDA_PARAM_NODE_COUNT);
    uint32_t start = (count >> 16) & 0xFF;
    uint32_t total = count & 0xFF;
    if (total == 0)
        return false;

    c->afg = afg;
    c->afg_in_amp = hda_param(c, afg, HDA_PARAM_IN_AMP_CAP);
    c->afg_out_amp = hda_param(c, afg, HDA_PARAM_OUT_AMP_CAP);
    hda_verb(c, afg, HDA_VERB_SET_POWER, 0);

    memset(c->present, 0, sizeof(c->present));
    for (uint32_t nid = start; nid < start + total && nid < HDA_MAX_NODES; nid++)
    {
        HdaWidget *w = &c->widgets[nid];
        memset(w, 0, sizeof(*w));
        w->caps = hda_param(c, (uint8_t)nid, HDA_PARAM_WIDGET_CAP);
        c->present[nid] = true;

        if (w->caps & HDA_WCAP_AMP_OVERRIDE)
        {
            w->in_amp = hda_param(c, (uint8_t)nid, HDA_PARAM_IN_AMP_CAP);
            w->out_amp = hda_param(c, (uint8_t)nid, HDA_PARAM_OUT_AMP_CAP);
        }
        else
        {
            w->in_amp = c->afg_in_amp;
            w->out_amp = c->afg_out_amp;
        }

        if (HDA_WCAP_TYPE(w->caps) == HDA_WIDGET_PIN)
        {
            w->pin_caps = hda_param(c, (uint8_t)nid, HDA_PARAM_PIN_CAP);
            w->config = hda_verb(c, (uint8_t)nid, HDA_VERB_GET_CONFIG, 0);
        }

        hda_read_connections(c, (uint8_t)nid, w);
    }
    return true;
}

static int hda_type(HdaCodec *c, uint8_t nid)
{
    return c->present[nid] ? (int)HDA_WCAP_TYPE(c->widgets[nid].caps) : -1;
}

static bool hda_is_digital(HdaCodec *c, uint8_t nid)
{
    return c->widgets[nid].caps & HDA_WCAP_DIGITAL;
}

typedef bool (*HdaPathGoal)(HdaCodec *c, uint8_t nid, int want);

static bool hda_find_path(HdaCodec *c, uint8_t nid, HdaPathGoal goal, int want, HdaPath *path)
{
    if (path->len >= HDA_MAX_PATH)
        return false;
    for (int i = 0; i < path->len; i++)
        if (path->nid[i] == nid)
            return false;

    int at = path->len++;
    path->nid[at] = nid;

    if (at > 0 && goal(c, nid, want))
        return true;

    int type = hda_type(c, nid);
    if (at == 0 || type == HDA_WIDGET_MIXER || type == HDA_WIDGET_SELECTOR)
    {
        HdaWidget *w = &c->widgets[nid];
        for (int i = 0; i < w->conn_count; i++)
        {
            path->sel[at] = (uint8_t)i;
            if (hda_find_path(c, w->conn[i], goal, want, path))
                return true;
        }
    }

    path->len--;
    return false;
}

static bool hda_goal_dac(HdaCodec *c, uint8_t nid, int want)
{
    (void)want;
    return hda_type(c, nid) == HDA_WIDGET_OUTPUT && !hda_is_digital(c, nid);
}

static bool hda_goal_pin(HdaCodec *c, uint8_t nid, int want)
{
    (void)c;
    return nid == want;
}

static uint32_t hda_amp_gain(uint32_t amp_caps)
{
    uint32_t offset = amp_caps & 0x7F;
    uint32_t steps = (amp_caps >> 8) & 0x7F;
    return MIN(offset, steps);
}

static void hda_unmute_out(HdaCodec *c, uint8_t nid)
{
    HdaWidget *w = &c->widgets[nid];
    if (w->caps & HDA_WCAP_OUT_AMP)
        hda_verb16(c, nid, HDA_VERB_SET_AMP, HDA_AMP_SET_OUT | HDA_AMP_SET_LEFT | HDA_AMP_SET_RIGHT |
                                             hda_amp_gain(w->out_amp));
}

static void hda_unmute_in(HdaCodec *c, uint8_t nid, uint8_t index)
{
    HdaWidget *w = &c->widgets[nid];
    if (w->caps & HDA_WCAP_IN_AMP)
        hda_verb16(c, nid, HDA_VERB_SET_AMP, HDA_AMP_SET_IN | HDA_AMP_SET_LEFT | HDA_AMP_SET_RIGHT |
                                             ((uint32_t)index << 8) | hda_amp_gain(w->in_amp));
}

static void hda_enable_path(HdaCodec *c, const HdaPath *path)
{
    for (int i = 0; i < path->len; i++)
    {
        uint8_t nid = path->nid[i];
        HdaWidget *w = &c->widgets[nid];

        hda_verb(c, nid, HDA_VERB_SET_POWER, 0);
        hda_unmute_out(c, nid);

        if (i + 1 < path->len)
        {
            if (hda_type(c, nid) != HDA_WIDGET_MIXER && w->conn_count > 1)
                hda_verb(c, nid, HDA_VERB_SET_CONN_SEL, path->sel[i]);
            hda_unmute_in(c, nid, path->sel[i]);
        }
        else
        {
            hda_unmute_in(c, nid, 0);
        }
    }
}

static const char *hda_type_name(uint8_t type, uint32_t config)
{
    bool fixed = HDA_CFG_CONN(config) == HDA_CONN_FIXED;
    switch (type)
    {
        case HDA_DEV_SPEAKER:  return "Speakers";
        case HDA_DEV_HP_OUT:   return "Headphones";
        case HDA_DEV_LINE_OUT: return fixed ? "Internal Output" : "Line Out";
        case HDA_DEV_MIC_IN:   return fixed ? "Internal Microphone" : "Microphone";
        case HDA_DEV_LINE_IN:  return "Line In";
        default:               return "Audio";
    }
}

static const char *hda_location_name(uint32_t config)
{
    if (HDA_CFG_CONN(config) == HDA_CONN_FIXED)
        return "";
    switch (HDA_CFG_LOCATION(config) & 0xF)
    {
        case 0x1: return "Rear ";
        case 0x2: return "Front ";
        case 0x3: return "Left ";
        case 0x4: return "Right ";
        case 0x5: return "Top ";
        case 0x6: return "Bottom ";
        default:  return "";
    }
}

static void hda_append(char *dst, size_t size, const char *src)
{
    size_t n = strlen(dst);
    while (*src && n + 1 < size)
        dst[n++] = *src++;
    dst[n] = '\0';
}

static void hda_append_num(char *dst, size_t size, uint32_t v, uint32_t base)
{
    char tmp[12], one[2] = { 0, 0 };
    int i = 0;
    do
    {
        tmp[i++] = "0123456789abcdef"[v % base];
        v /= base;
    }
    while (v && i < 12);
    while (i)
    {
        one[0] = tmp[--i];
        hda_append(dst, size, one);
    }
}

static void hda_add_dev(HdaCodec *c, bool input, uint8_t pin, uint8_t conv, const HdaPath *path)
{
    if (hda.n_devs >= AUDIO_MAX_DEVICES)
        return;
    HdaWidget *w = &c->widgets[pin];
    HdaDev *d = &hda.devs[hda.n_devs];
    memset(d, 0, sizeof(*d));
    d->codec = c;
    d->input = input;
    d->pin = pin;
    d->conv = conv;
    d->type = (uint8_t)HDA_CFG_DEVICE(w->config);
    d->path = *path;
    hda_append(d->name, sizeof(d->name), hda_location_name(w->config));
    hda_append(d->name, sizeof(d->name), hda_type_name(d->type, w->config));

    int same = 0;
    for (int i = 0; i < hda.n_devs; i++)
        if (strncmp(hda.devs[i].name, d->name, strlen(d->name)) == 0)
            same++;
    if (same)
    {
        hda_append(d->name, sizeof(d->name), " ");
        hda_append_num(d->name, sizeof(d->name), (uint32_t)same + 1, 10);
    }

    const PciDevice *pci = &c->ctl->pci;
    hda_append_num(d->key, sizeof(d->key), pci->bus, 16);
    hda_append(d->key, sizeof(d->key), ":");
    hda_append_num(d->key, sizeof(d->key), pci->slot, 16);
    hda_append(d->key, sizeof(d->key), ".");
    hda_append_num(d->key, sizeof(d->key), pci->func, 16);
    hda_append(d->key, sizeof(d->key), "/");
    hda_append_num(d->key, sizeof(d->key), c->cad, 10);
    hda_append(d->key, sizeof(d->key), "/");
    hda_append_num(d->key, sizeof(d->key), pin, 10);
    hda.n_devs++;

    kprintf("hda: %s \"%s\" (%s) via %s %d\n", input ? "input" : "output", d->name, d->key,
            input ? "ADC" : "DAC", conv);
}

static void hda_find_devices(HdaCodec *c)
{
    for (int nid = 0; nid < HDA_MAX_NODES; nid++)
    {
        if (hda_type(c, (uint8_t)nid) != HDA_WIDGET_PIN || hda_is_digital(c, (uint8_t)nid))
            continue;
        HdaWidget *w = &c->widgets[nid];
        if (HDA_CFG_CONN(w->config) == HDA_CONN_NONE)
            continue;
        int type = (int)HDA_CFG_DEVICE(w->config);

        if ((w->pin_caps & HDA_PINCAP_OUT) &&
            (type == HDA_DEV_SPEAKER || type == HDA_DEV_HP_OUT || type == HDA_DEV_LINE_OUT))
        {
            HdaPath path = { 0 };
            if (hda_find_path(c, (uint8_t)nid, hda_goal_dac, 0, &path))
                hda_add_dev(c, false, (uint8_t)nid, path.nid[path.len - 1], &path);
        }

        if ((w->pin_caps & HDA_PINCAP_IN) && (type == HDA_DEV_MIC_IN || type == HDA_DEV_LINE_IN))
        {
            for (int adc = 0; adc < HDA_MAX_NODES; adc++)
            {
                if (hda_type(c, (uint8_t)adc) != HDA_WIDGET_INPUT || hda_is_digital(c, (uint8_t)adc))
                    continue;
                HdaPath path = { 0 };
                if (hda_find_path(c, (uint8_t)adc, hda_goal_pin, nid, &path))
                {
                    hda_add_dev(c, true, (uint8_t)nid, (uint8_t)adc, &path);
                    break;
                }
            }
        }
    }
}

static void hda_scan_codec(HdaCtl *ctl, uint8_t cad)
{
    if (hda.n_codecs >= HDA_MAX_CODECS)
        return;
    HdaCodec *c = &hda_codecs[hda.n_codecs];
    memset(c, 0, sizeof(*c));
    c->ctl = ctl;
    c->cad = cad;

    uint32_t vendor = hda_param(c, 0, HDA_PARAM_VENDOR);
    uint32_t count = hda_param(c, 0, HDA_PARAM_NODE_COUNT);
    uint32_t start = (count >> 16) & 0xFF;
    uint32_t total = count & 0xFF;
    kprintf("hda: codec %d vendor %x:%x\n", cad, vendor >> 16, vendor & 0xFFFF);

    for (uint32_t fg = start; fg < start + total; fg++)
    {
        if ((hda_param(c, (uint8_t)fg, HDA_PARAM_FG_TYPE) & 0xFF) != HDA_FG_AUDIO)
            continue;
        if (!hda_scan_afg(c, (uint8_t)fg))
            continue;
        int before = hda.n_devs;
        hda_find_devices(c);
        if (hda.n_devs > before)
            hda.n_codecs++;
        return;
    }
}

static bool hda_stream_alloc(HdaCtl *c, HdaStream *s, int index, uint8_t tag)
{
    s->regs = c->mmio + HDA_REG_SD_BASE + index * HDA_SD_STRIDE;
    s->dpos = c->dpos ? c->dpos + index * 2 : NULL;
    s->tag = tag;
    s->buf = hda_dma_alloc(c, HDA_BUF_PAGES);
    s->bdl = hda_dma_alloc(c, 1);
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
    if (s->dpos)
        *s->dpos = 0;

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
    if (!s || !s->running)
        return;
    uint32_t lpib = s->dpos ? *s->dpos : HDA_BUF_SIZE;
    if (lpib >= HDA_BUF_SIZE)
        lpib = sd_rd32(s, HDA_SD_LPIB);
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
        int16_t *dst = (int16_t *)(hda.out->buf + (at + i) % HDA_BUF_SIZE);
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
    uint64_t target = (hda.out->pos + HDA_PLAY_LEAD) & ~(uint64_t)(HDA_FRAME - 1);
    if (hda.fill < hda.out->pos)
        hda.fill = ALIGN_UP(hda.out->pos, HDA_FRAME);
    if (target <= hda.fill)
        return;

    uint32_t bytes = (uint32_t)(target - hda.fill);
    for (HdaPlayClient *c = hda.play_clients; c; c = c->next)
        hda_mix_client(c, hda.fill, bytes);
    hda.fill = target;
}

static void hda_play_start(void)
{
    if (!hda.out || hda.out->running)
        return;
    memset(hda.out->buf, 0, HDA_BUF_SIZE);
    hda.fill = 0;
    hda.out->pos = 0;
    hda_play_fill();
    hda_stream_start(hda.out);
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

    if (!hda.out || !hda.out->running)
        return;

    uint64_t old = hda.out->pos;
    hda_stream_update(hda.out);
    hda_ring_zero(hda.out->buf, old, hda.out->pos);
    hda_play_fill();

    if (hda.play_clients)
        hda.idle_since = now;
    else if (now - hda.idle_since > HDA_IDLE_STOP_MS)
        hda_stream_stop(hda.out);
}

static void hda_capture_trim(HdaCaptureClient *c)
{
    if (hda.in->pos - c->pos > HDA_CAPTURE_KEEP)
        c->pos = hda.in->pos - HDA_CAPTURE_KEEP;
}

static HdaCaptureClient *hda_capture_open(void)
{
    HdaCaptureClient *c = kmalloc(sizeof(*c));
    if (!c)
        return NULL;

    uint64_t flags = irq_save();
    if (!hda.in->running)
        hda_stream_start(hda.in);
    hda_stream_update(hda.in);
    c->pos = hda.in->pos;
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
    if (!hda.capture_clients && hda.in && hda.in->running)
        hda_stream_stop(hda.in);
    irq_restore(flags);
    kfree(c);
}

static void hda_select_output(int id)
{
    HdaDev *old = hda.out_dev >= 0 ? &hda.devs[hda.out_dev] : NULL;
    HdaDev *d = &hda.devs[id];
    HdaCodec *c = d->codec;

    if (old && old != d)
    {
        hda_verb(old->codec, old->pin, HDA_VERB_SET_PIN_CTL, 0);
        if (old->codec != c || old->conv != d->conv)
            hda_verb(old->codec, old->conv, HDA_VERB_SET_STREAM, 0);
    }

    hda_enable_path(c, &d->path);
    HdaWidget *w = &c->widgets[d->pin];
    uint8_t ctl = HDA_PINCTL_OUT;
    if (d->type == HDA_DEV_HP_OUT && (w->pin_caps & HDA_PINCAP_HP_DRIVE))
        ctl |= HDA_PINCTL_HP;
    hda_verb(c, d->pin, HDA_VERB_SET_PIN_CTL, ctl);
    if (w->pin_caps & HDA_PINCAP_EAPD)
        hda_verb(c, d->pin, HDA_VERB_SET_EAPD, 0x2);
    hda_verb16(c, d->conv, HDA_VERB_SET_FORMAT, HDA_FORMAT);
    hda_verb(c, d->conv, HDA_VERB_SET_STREAM, (uint32_t)HDA_STREAM_TAG_OUT << 4);

    HdaStream *stream = &c->ctl->out;
    if (hda.out != stream)
    {
        bool was_running = hda.out && hda.out->running;
        if (was_running)
            hda_stream_stop(hda.out);
        hda.out = stream;
        if (was_running || hda.play_clients)
            hda_play_start();
    }
    hda.out_dev = id;
}

static void hda_select_input(int id)
{
    HdaDev *old = hda.in_dev >= 0 ? &hda.devs[hda.in_dev] : NULL;
    HdaDev *d = &hda.devs[id];
    HdaCodec *c = d->codec;

    if (old && old != d)
    {
        if (old->codec != c || old->pin != d->pin)
            hda_verb(old->codec, old->pin, HDA_VERB_SET_PIN_CTL, 0);
        if (old->codec != c || old->conv != d->conv)
            hda_verb(old->codec, old->conv, HDA_VERB_SET_STREAM, 0);
    }

    hda_enable_path(c, &d->path);
    HdaWidget *w = &c->widgets[d->pin];
    uint8_t ctl = HDA_PINCTL_IN;
    if (d->type == HDA_DEV_MIC_IN)
    {
        if (w->pin_caps & HDA_PINCAP_VREF_80)
            ctl |= HDA_PINCTL_VREF_80;
        else if (w->pin_caps & HDA_PINCAP_VREF_50)
            ctl |= HDA_PINCTL_VREF_50;
    }
    hda_verb(c, d->pin, HDA_VERB_SET_PIN_CTL, ctl);
    hda_verb16(c, d->conv, HDA_VERB_SET_FORMAT, HDA_FORMAT);
    hda_verb(c, d->conv, HDA_VERB_SET_STREAM, (uint32_t)HDA_STREAM_TAG_IN << 4);

    HdaStream *stream = &c->ctl->in;
    if (hda.in != stream)
    {
        if (hda.in && hda.in->running)
            hda_stream_stop(hda.in);
        hda.in = stream;
        if (hda.capture_clients)
        {
            hda_stream_start(hda.in);
            for (HdaCaptureClient *cc = hda.capture_clients; cc; cc = cc->next)
                cc->pos = 0;
        }
    }
    hda.in_dev = id;
}

static int hda_default_dev(bool input)
{
    static const int out_pref[] = { HDA_DEV_SPEAKER, HDA_DEV_LINE_OUT, HDA_DEV_HP_OUT };
    static const int in_pref[] = { HDA_DEV_MIC_IN, HDA_DEV_LINE_IN };
    const int *pref = input ? in_pref : out_pref;
    int n = input ? (int)ARRAY_LEN(in_pref) : (int)ARRAY_LEN(out_pref);
    for (int p = 0; p < n; p++)
        for (int i = 0; i < hda.n_devs; i++)
            if (hda.devs[i].input == input && hda.devs[i].type == pref[p])
                return i;
    return -1;
}

void hda_poll(void)
{
    if (!hda.ready)
        return;

    uint64_t out_before = hda.out ? hda.out->pos / HDA_PERIOD : 0;
    uint64_t in_before = hda.in ? hda.in->pos / HDA_PERIOD : 0;

    hda_play_poll(task_uptime_ms());
    hda_stream_update(hda.in);

    if ((hda.out && hda.out->pos / HDA_PERIOD != out_before) || (hda.in && hda.in->pos / HDA_PERIOD != in_before))
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
        if (!hda.in || !(f->capture = hda_capture_open()))
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
    if (!f || !hda.out || (file->flags & 0x3) == VFS_O_RDONLY)
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

static void hda_apply_gain(int16_t *s, size_t samples)
{
    int32_t vol = (int32_t)hda.in_volume;
    if (vol == HDA_UNITY)
        return;
    for (size_t i = 0; i < samples; i++)
    {
        int32_t v = (int32_t)s[i] * vol >> 8;
        s[i] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
    }
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
        hda_stream_update(hda.in);
        hda_capture_trim(c);

        if (hda.in->pos - c->pos >= count)
        {
            uint8_t *dst = (uint8_t *)buf;
            size_t off = (size_t)(c->pos % HDA_BUF_SIZE);
            size_t first = MIN(count, HDA_BUF_SIZE - off);
            memcpy(dst, hda.in->buf + off, first);
            memcpy(dst + first, hda.in->buf, count - first);
            c->pos += count;
            irq_restore(flags);
            hda_apply_gain((int16_t *)buf, count / 2);
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

    if (hda.out && (file->flags & 0x3) != VFS_O_RDONLY)
    {
        if (!f->play || HDA_CLIENT_RING - (f->play->head - f->play->tail) >= HDA_PERIOD)
            events |= POLLOUT;
    }

    if (f->capture)
    {
        hda_stream_update(hda.in);
        hda_capture_trim(f->capture);
        if (hda.in->pos - f->capture->pos >= HDA_PERIOD)
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
                .playback     = hda.out != NULL,
                .capture      = hda.in != NULL,
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
                hda_stream_update(hda.in);
                hda_capture_trim(f->capture);
                state.capture_avail = (uint32_t)(hda.in->pos - f->capture->pos);
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
            hda.volume = v * HDA_UNITY / 100;
            return 0;
        }
        case DEVFS_IOCTL_AUDIO_GET_VOLUME:
        {
            if (!arg)
                return -1;
            *(uint32_t *)arg = (hda.volume * 100 + HDA_UNITY / 2) / HDA_UNITY;
            return 0;
        }
        case DEVFS_IOCTL_AUDIO_SET_INPUT_VOLUME:
        {
            if (!arg)
                return -1;
            uint32_t v = MIN(*(uint32_t *)arg, 200u);
            hda.in_volume = v * HDA_UNITY / 100;
            return 0;
        }
        case DEVFS_IOCTL_AUDIO_GET_INPUT_VOLUME:
        {
            if (!arg)
                return -1;
            *(uint32_t *)arg = (hda.in_volume * 100 + HDA_UNITY / 2) / HDA_UNITY;
            return 0;
        }
        case DEVFS_IOCTL_AUDIO_DROP:
        {
            uint64_t flags = irq_save();
            if (f->play)
                f->play->tail = f->play->head;
            if (f->capture)
            {
                hda_stream_update(hda.in);
                f->capture->pos = hda.in->pos;
            }
            irq_restore(flags);
            return 0;
        }
        case DEVFS_IOCTL_AUDIO_LIST_DEVICES:
        {
            if (!arg)
                return -1;
            AudioDeviceList *list = (AudioDeviceList *)arg;
            memset(list, 0, sizeof(*list));
            for (int i = 0; i < hda.n_devs; i++)
            {
                AudioDevice *d = &list->devices[list->count++];
                d->id = (uint32_t)i;
                d->input = hda.devs[i].input;
                d->selected = i == (hda.devs[i].input ? hda.in_dev : hda.out_dev);
                d->type = hda.devs[i].type;
                memcpy(d->name, hda.devs[i].name, sizeof(d->name));
                memcpy(d->key, hda.devs[i].key, sizeof(d->key));
            }
            return 0;
        }
        case DEVFS_IOCTL_AUDIO_SET_OUTPUT_DEVICE:
        case DEVFS_IOCTL_AUDIO_SET_INPUT_DEVICE:
        {
            if (!arg)
                return -1;
            uint32_t id = *(uint32_t *)arg;
            bool input = request == DEVFS_IOCTL_AUDIO_SET_INPUT_DEVICE;
            if (id >= (uint32_t)hda.n_devs || hda.devs[id].input != input)
                return -1;
            HdaCtl *ctl = hda.devs[id].codec->ctl;
            if (input ? !ctl->in_ok : !ctl->out_ok)
                return -1;
            uint64_t flags = irq_save();
            if (input)
                hda_select_input((int)id);
            else
                hda_select_output((int)id);
            irq_restore(flags);
            return 0;
        }
        default:
            return -1;
    }
}

static bool hda_init_controller(const PciDevice *dev)
{
    if (hda.n_ctls >= HDA_MAX_CTLS)
        return false;
    pci_enable_device(dev);
    if (pci_bar_is_io(dev->bar[0]))
    {
        kprintf("hda: BAR0 is not MMIO\n");
        return false;
    }

    HdaCtl *c = &hda_ctls[hda.n_ctls];
    memset(c, 0, sizeof(*c));
    c->pci = *dev;
    c->mmio = (volatile uint8_t *)vmm_map_mmio(pci_bar_addr(dev, 0), 0x4000);

    uint16_t gcap = hda_rd16(c, HDA_REG_GCAP);
    c->iss = (gcap >> 8) & 0xF;
    c->oss = (gcap >> 12) & 0xF;
    c->dma64 = gcap & 1;

    kprintf("hda: controller %x:%x at %x:%x.%x v%d.%d, %d in / %d out streams\n", dev->vendor_id, dev->device_id,
            dev->bus, dev->slot, dev->func, hda_rd8(c, HDA_REG_VMAJ), hda_rd8(c, HDA_REG_VMIN), c->iss, c->oss);

    if (!hda_reset_controller(c))
        return false;

    hda_wr32(c, HDA_REG_INTCTL, 0);
    hda_wr32(c, HDA_REG_DPLBASE, 0);
    hda_wr32(c, HDA_REG_DPUBASE, 0);

    if (!hda_setup_rings(c))
    {
        kprintf("hda: failed to allocate command rings\n");
        return false;
    }

    uint8_t *dpos = hda_dma_alloc(c, 1);
    if (dpos)
    {
        uint64_t phys = virt_to_phys(dpos);
        c->dpos = (volatile uint32_t *)dpos;
        hda_wr32(c, HDA_REG_DPUBASE, (uint32_t)(phys >> 32));
        hda_wr32(c, HDA_REG_DPLBASE, (uint32_t)phys | 1);
    }

    uint16_t codecs = hda_rd16(c, HDA_REG_STATESTS);
    hda_wr16(c, HDA_REG_STATESTS, codecs);

    int before = hda.n_devs;
    for (uint8_t cad = 0; cad < 15; cad++)
        if (codecs & (1u << cad))
            hda_scan_codec(c, cad);

    bool want_out = false, want_in = false;
    for (int i = before; i < hda.n_devs; i++)
    {
        if (hda.devs[i].input)
            want_in = true;
        else
            want_out = true;
    }
    if (want_out && c->oss > 0)
        c->out_ok = hda_stream_alloc(c, &c->out, c->iss, HDA_STREAM_TAG_OUT);
    if (want_in && c->iss > 0)
        c->in_ok = hda_stream_alloc(c, &c->in, 0, HDA_STREAM_TAG_IN);

    for (int i = before; i < hda.n_devs; i++)
    {
        if (hda.devs[i].input ? c->in_ok : c->out_ok)
            continue;
        memmove(&hda.devs[i], &hda.devs[i + 1], sizeof(HdaDev) * (size_t)(hda.n_devs - i - 1));
        hda.n_devs--;
        i--;
    }

    if (hda.n_devs == before)
    {
        kprintf("hda: no usable analog devices (STATESTS %x)\n", codecs);
        return false;
    }
    hda.n_ctls++;
    return true;
}

static void hda_find_cb(const PciDevice *dev, void *ctx)
{
    (void)ctx;
    if (dev->class_code == 0x04 && dev->subclass == 0x03)
        hda_init_controller(dev);
}

int hda_init(void)
{
    memset(&hda, 0, sizeof(hda));
    hda.volume = HDA_UNITY;
    hda.in_volume = HDA_UNITY;
    hda.out_dev = hda.in_dev = -1;

    pci_enumerate(hda_find_cb, NULL);
    if (hda.n_devs == 0)
    {
        kprintf("hda: no HD Audio devices found\n");
        return 0;
    }

    int out = hda_default_dev(false);
    int in = hda_default_dev(true);
    if (out >= 0)
        hda_select_output(out);
    if (in >= 0)
        hda_select_input(in);

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
    kprintf("hda: /dev/audio ready, output \"%s\", input \"%s\"\n", out >= 0 ? hda.devs[out].name : "none",
            in >= 0 ? hda.devs[in].name : "none");
    return 1;
}

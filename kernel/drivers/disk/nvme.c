#define NVME_REG_CAP   0x00
#define NVME_REG_CC    0x14
#define NVME_REG_CSTS  0x1C
#define NVME_REG_AQA   0x24
#define NVME_REG_ASQ   0x28
#define NVME_REG_ACQ   0x30

#define NVME_CC_EN     (1u << 0)
#define NVME_CSTS_RDY  (1u << 0)
#define NVME_CSTS_CFS  (1u << 1)

#define NVME_QUEUE_SIZE 64

#define NVME_ADMIN_CREATE_SQ 0x01
#define NVME_ADMIN_CREATE_CQ 0x05
#define NVME_ADMIN_IDENTIFY  0x06

#define NVME_CMD_FLUSH 0x00
#define NVME_CMD_WRITE 0x01
#define NVME_CMD_READ  0x02

#define NVME_MAX_CONTROLLERS 4
#define NVME_MAX_NAMESPACES  4

typedef struct PACKED
{
    uint32_t cdw0;
    uint32_t nsid;
    uint64_t reserved;
    uint64_t mptr;
    uint64_t prp1;
    uint64_t prp2;
    uint32_t cdw10;
    uint32_t cdw11;
    uint32_t cdw12;
    uint32_t cdw13;
    uint32_t cdw14;
    uint32_t cdw15;
}
NvmeCmd;

typedef struct PACKED
{
    uint32_t result;
    uint32_t reserved;
    uint16_t sq_head;
    uint16_t sq_id;
    uint16_t cid;
    uint16_t status;
}
NvmeCpl;

typedef struct
{
    volatile NvmeCmd  *sq;
    volatile NvmeCpl  *cq;
    volatile uint32_t *sq_db;
    volatile uint32_t *cq_db;
    uint16_t tail;
    uint16_t head;
    uint16_t cid;
    uint8_t  phase;
}
NvmeQueue;

typedef struct
{
    volatile uint8_t *regs;
    NvmeQueue admin;
    NvmeQueue io;
    uint64_t *prp_list;
    uint8_t  *scratch;
    char      model[BLOCK_MODEL_MAX];
}
NvmeController;

typedef struct
{
    NvmeController *ctrl;
    uint32_t nsid;
}
NvmeNamespace;

static NvmeController nvme_controllers[NVME_MAX_CONTROLLERS];
static NvmeNamespace nvme_namespaces[NVME_MAX_CONTROLLERS * NVME_MAX_NAMESPACES];
static int nvme_controller_count;
static int nvme_namespace_count;

static inline uint32_t nvme_read32(NvmeController *c, uint32_t off)
{
    return *(volatile uint32_t *)(c->regs + off);
}

static inline void nvme_write32(NvmeController *c, uint32_t off, uint32_t val)
{
    *(volatile uint32_t *)(c->regs + off) = val;
}

static inline void nvme_write64(NvmeController *c, uint32_t off, uint64_t val)
{
    nvme_write32(c, off, (uint32_t)val);
    nvme_write32(c, off + 4, (uint32_t)(val >> 32));
}

static bool nvme_queue_init(NvmeController *c, NvmeQueue *q, int qid, uint32_t stride)
{
    void *sq = pmm_alloc_page();
    void *cq = pmm_alloc_page();
    if (!sq || !cq)
        return false;
    memset(sq, 0, PAGE_SIZE);
    memset(cq, 0, PAGE_SIZE);

    q->sq = sq;
    q->cq = cq;
    q->sq_db = (volatile uint32_t *)(c->regs + 0x1000 + (2 * qid) * stride);
    q->cq_db = (volatile uint32_t *)(c->regs + 0x1000 + (2 * qid + 1) * stride);
    q->tail = 0;
    q->head = 0;
    q->cid = 0;
    q->phase = 1;
    return true;
}

static int nvme_submit(NvmeQueue *q, NvmeCmd *cmd, uint32_t *result)
{
    uint16_t cid = q->cid++;
    cmd->cdw0 = (cmd->cdw0 & 0xFFFF) | ((uint32_t)cid << 16);

    volatile uint32_t *dst = (volatile uint32_t *)&q->sq[q->tail];
    const uint32_t *src = (const uint32_t *)cmd;
    for (size_t i = 0; i < sizeof(NvmeCmd) / 4; i++)
        dst[i] = src[i];

    q->tail = (uint16_t)((q->tail + 1) % NVME_QUEUE_SIZE);
    __asm__ volatile("mfence" ::: "memory");
    *q->sq_db = q->tail;

    for (uint64_t i = 0; i < 20000000; i++)
    {
        volatile NvmeCpl *e = &q->cq[q->head];
        uint16_t status = e->status;
        if ((status & 1) == q->phase)
        {
            uint16_t ecid = e->cid;
            if (result)
                *result = e->result;
            q->head = (uint16_t)((q->head + 1) % NVME_QUEUE_SIZE);
            if (q->head == 0)
                q->phase ^= 1;
            *q->cq_db = q->head;
            if (ecid != cid)
                continue;
            return (status >> 1) ? -1 : 0;
        }
        inb(0x80);
    }
    return -1;
}

static int nvme_identify(NvmeController *c, uint32_t nsid, uint32_t cns)
{
    NvmeCmd cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.cdw0 = NVME_ADMIN_IDENTIFY;
    cmd.nsid = nsid;
    cmd.prp1 = virt_to_phys(c->scratch);
    cmd.cdw10 = cns;
    return nvme_submit(&c->admin, &cmd, NULL);
}

static int nvme_io(BlockDevice *dev, uint64_t lba, uint32_t count, void *buf, bool write)
{
    NvmeNamespace *ns = dev->driver_data;
    NvmeController *c = ns->ctrl;

    uint64_t phys = virt_to_phys(buf);
    size_t bytes = (size_t)count * BLOCK_SECTOR_SIZE;
    size_t pages = DIV_CEIL((phys & (PAGE_SIZE - 1)) + bytes, PAGE_SIZE);

    NvmeCmd cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.cdw0 = write ? NVME_CMD_WRITE : NVME_CMD_READ;
    cmd.nsid = ns->nsid;
    cmd.prp1 = phys;
    uint64_t next = ALIGN_DOWN(phys, PAGE_SIZE) + PAGE_SIZE;
    if (pages == 2)
    {
        cmd.prp2 = next;
    }
    else if (pages > 2)
    {
        for (size_t i = 0; i < pages - 1; i++)
            c->prp_list[i] = next + i * PAGE_SIZE;
        cmd.prp2 = virt_to_phys(c->prp_list);
    }
    cmd.cdw10 = (uint32_t)lba;
    cmd.cdw11 = (uint32_t)(lba >> 32);
    cmd.cdw12 = count - 1;
    return nvme_submit(&c->io, &cmd, NULL);
}

static int nvme_flush(BlockDevice *dev)
{
    NvmeNamespace *ns = dev->driver_data;
    NvmeCmd cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.cdw0 = NVME_CMD_FLUSH;
    cmd.nsid = ns->nsid;
    return nvme_submit(&ns->ctrl->io, &cmd, NULL);
}

static void nvme_add_namespace(NvmeController *c, uint32_t nsid)
{
    if (nvme_namespace_count >= (int)ARRAY_LEN(nvme_namespaces))
        return;
    if (nvme_identify(c, nsid, 0) != 0)
        return;

    uint64_t nsze;
    memcpy(&nsze, c->scratch, 8);
    uint8_t flbas = c->scratch[26] & 0x0F;
    uint32_t lbaf;
    memcpy(&lbaf, c->scratch + 128 + flbas * 4, 4);
    uint32_t lbads = (lbaf >> 16) & 0xFF;

    if (nsze == 0)
        return;
    if (lbads != 9)
    {
        kprintf("nvme: namespace %u uses %u-byte sectors, skipping\n", nsid, 1u << lbads);
        return;
    }

    NvmeNamespace *ns = &nvme_namespaces[nvme_namespace_count++];
    ns->ctrl = c;
    ns->nsid = nsid;
    block_register(c->model, DISK_KIND_NVME, nsze, nvme_io, nvme_flush, ns);
}

static void nvme_init_controller(const PciDevice *dev)
{
    if (nvme_controller_count >= NVME_MAX_CONTROLLERS)
        return;

    uint64_t bar = pci_bar_addr(dev, 0);
    if (!bar || pci_bar_is_io(dev->bar[0]))
        return;

    pci_enable_device(dev);

    NvmeController *c = &nvme_controllers[nvme_controller_count];
    memset(c, 0, sizeof(*c));
    c->regs = (volatile uint8_t *)vmm_map_mmio(bar, 0x4000);

    uint64_t cap = (uint64_t)nvme_read32(c, NVME_REG_CAP) | ((uint64_t)nvme_read32(c, NVME_REG_CAP + 4) << 32);
    uint32_t stride = 4u << ((cap >> 32) & 0xF);
    uint32_t timeout = (uint32_t)((cap >> 24) & 0xFF) * 500 + 500;
    if (stride * 4 > 0x3000)
    {
        kprintf("nvme: doorbell stride too large\n");
        return;
    }

    nvme_write32(c, NVME_REG_CC, nvme_read32(c, NVME_REG_CC) & ~NVME_CC_EN);
    if (!block_wait32((volatile uint32_t *)(c->regs + NVME_REG_CSTS), NVME_CSTS_RDY, 0, timeout))
    {
        kprintf("nvme: controller did not reset\n");
        return;
    }

    c->prp_list = pmm_alloc_page();
    c->scratch = pmm_alloc_page();
    if (!c->prp_list || !c->scratch ||
        !nvme_queue_init(c, &c->admin, 0, stride) || !nvme_queue_init(c, &c->io, 1, stride))
        return;

    nvme_write32(c, NVME_REG_AQA, ((NVME_QUEUE_SIZE - 1) << 16) | (NVME_QUEUE_SIZE - 1));
    nvme_write64(c, NVME_REG_ASQ, virt_to_phys((void *)c->admin.sq));
    nvme_write64(c, NVME_REG_ACQ, virt_to_phys((void *)c->admin.cq));
    nvme_write32(c, NVME_REG_CC, NVME_CC_EN | (6u << 16) | (4u << 20));

    if (!block_wait32((volatile uint32_t *)(c->regs + NVME_REG_CSTS), NVME_CSTS_RDY | NVME_CSTS_CFS, NVME_CSTS_RDY, timeout))
    {
        kprintf("nvme: controller failed to start\n");
        return;
    }

    if (nvme_identify(c, 0, 1) != 0)
    {
        kprintf("nvme: identify controller failed\n");
        return;
    }
    memcpy(c->model, c->scratch + 24, 40);
    c->model[40] = '\0';
    for (int i = 39; i >= 0 && (c->model[i] == ' ' || c->model[i] == '\0'); i--)
        c->model[i] = '\0';
    uint32_t nn;
    memcpy(&nn, c->scratch + 516, 4);

    NvmeCmd cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.cdw0 = NVME_ADMIN_CREATE_CQ;
    cmd.prp1 = virt_to_phys((void *)c->io.cq);
    cmd.cdw10 = ((NVME_QUEUE_SIZE - 1) << 16) | 1;
    cmd.cdw11 = 1;
    if (nvme_submit(&c->admin, &cmd, NULL) != 0)
    {
        kprintf("nvme: failed to create the completion queue\n");
        return;
    }

    memset(&cmd, 0, sizeof(cmd));
    cmd.cdw0 = NVME_ADMIN_CREATE_SQ;
    cmd.prp1 = virt_to_phys((void *)c->io.sq);
    cmd.cdw10 = ((NVME_QUEUE_SIZE - 1) << 16) | 1;
    cmd.cdw11 = (1u << 16) | 1;
    if (nvme_submit(&c->admin, &cmd, NULL) != 0)
    {
        kprintf("nvme: failed to create the submission queue\n");
        return;
    }

    nvme_controller_count++;
    for (uint32_t ns = 1; ns <= nn && ns <= NVME_MAX_NAMESPACES; ns++)
        nvme_add_namespace(c, ns);
}

static void nvme_pci_cb(const PciDevice *dev, void *ctx)
{
    (void)ctx;
    if (dev->class_code == 0x01 && dev->subclass == 0x08 && dev->prog_if == 0x02)
        nvme_init_controller(dev);
}

int nvme_init(void)
{
    pci_enumerate(nvme_pci_cb, NULL);
    return nvme_namespace_count;
}

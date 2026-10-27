static bool elf_check_header(const Elf64Ehdr *eh)
{
    if (eh->e_ident[0] != ELF_MAG0 || eh->e_ident[1] != ELF_MAG1 ||
        eh->e_ident[2] != ELF_MAG2 || eh->e_ident[3] != ELF_MAG3)
    {
        kprintf("elf: bad magic\n");
        return false;
    }

    if (eh->e_ident[4] != ELFCLASS64)
    {
        kprintf("elf: not a 64-bit ELF\n");
        return false;
    }

    if (eh->e_ident[5] != ELFDATA2LSB)
    {
        kprintf("elf: not little-endian\n");
        return false;
    }

    if (eh->e_machine != EM_X86_64)
    {
        kprintf("elf: not x86_64\n");
        return false;
    }

    if (eh->e_type != ET_EXEC && eh->e_type != ET_DYN)
    {
        kprintf("elf: unsupported e_type %d\n", (int)eh->e_type);
        return false;
    }

    if (eh->e_phoff == 0 || eh->e_phnum == 0)
    {
        kprintf("elf: no program headers\n");
        return false;
    }

    return true;
}

static int elf_apply_rela_table(
    const Elf64Rela *relas,
    size_t rela_count,
    const Elf64Sym *symtab,
    uint64_t load_bias
)
{
    for (size_t r = 0; r < rela_count; r++)
    {
        const Elf64Rela *rel = &relas[r];
        uint32_t type = ELF64_R_TYPE(rel->r_info);
        uint64_t *target = (uint64_t *)(rel->r_offset + load_bias);

        if (type == R_X86_64_NONE)
            continue;

        if (type == R_X86_64_RELATIVE)
        {
            *target = (uint64_t)(rel->r_addend + (int64_t)load_bias);
            continue;
        }

        if (type != R_X86_64_64 && type != R_X86_64_GLOB_DAT && type != R_X86_64_JUMP_SLOT)
        {
            kprintf("elf: unsupported relocation type %u at index %zu\n",
                    type, r);
            return -1;
        }

        uint32_t sym_index = ELF64_R_SYM(rel->r_info);
        if (!symtab || sym_index == 0 || symtab[sym_index].st_shndx == SHN_UNDEF)
        {
            kprintf("elf: relocation %zu references an undefined symbol\n", r);
            return -1;
        }

        uint64_t value = symtab[sym_index].st_value + load_bias;
        if (type == R_X86_64_64)
            value += (uint64_t)rel->r_addend;
        *target = value;
    }

    return 0;
}

static int elf_apply_relocations(
    const uint8_t *bytes,
    size_t size,
    const Elf64Phdr *phdrs,
    uint16_t phnum,
    uint64_t load_bias
)
{
    uint64_t rela_vaddr = 0;
    uint64_t rela_size  = 0;
    uint64_t rela_ent   = sizeof(Elf64Rela);
    uint64_t jmprel_vaddr = 0;
    uint64_t jmprel_size  = 0;
    uint64_t symtab_vaddr = 0;
    bool have_dynamic   = false;

    for (uint16_t i = 0; i < phnum; i++)
    {
        const Elf64Phdr *ph = &phdrs[i];
        if (ph->p_type != PT_DYNAMIC)
            continue;

        have_dynamic = true;

        if (ph->p_offset + ph->p_filesz > size)
        {
            kprintf("elf: PT_DYNAMIC out of bounds\n");
            return -1;
        }

        const Elf64Dyn *dyn = (const Elf64Dyn *)(bytes + ph->p_offset);
        size_t dyn_count = ph->p_filesz / sizeof(Elf64Dyn);

        for (size_t d = 0; d < dyn_count; d++)
        {
            if (dyn[d].d_tag == DT_NULL)
                break;

            switch (dyn[d].d_tag)
            {
                case DT_RELA:
                    rela_vaddr = dyn[d].d_val;
                    break;
                case DT_RELASZ:
                    rela_size = dyn[d].d_val;
                    break;
                case DT_RELAENT:
                    rela_ent = dyn[d].d_val;
                    break;
                case DT_JMPREL:
                    jmprel_vaddr = dyn[d].d_val;
                    break;
                case DT_PLTRELSZ:
                    jmprel_size = dyn[d].d_val;
                    break;
                case DT_SYMTAB:
                    symtab_vaddr = dyn[d].d_val;
                    break;
                case DT_REL:
                    kprintf("elf: DT_REL (non-addend) relocations unsupported\n");
                    return -1;
                default:
                    break;
            }
        }

        break;
    }

    if (!have_dynamic)
        return 0;

    if (rela_ent != sizeof(Elf64Rela))
    {
        kprintf("elf: unexpected DT_RELAENT %llu\n",
                (unsigned long long)rela_ent);
        return -1;
    }

    const Elf64Sym *symtab = symtab_vaddr ? (const Elf64Sym *)(symtab_vaddr + load_bias) : NULL;

    if (rela_vaddr && rela_size &&
        elf_apply_rela_table((const Elf64Rela *)(rela_vaddr + load_bias),
                             rela_size / rela_ent, symtab, load_bias) != 0)
        return -1;

    if (jmprel_vaddr && jmprel_size &&
        elf_apply_rela_table((const Elf64Rela *)(jmprel_vaddr + load_bias),
                             jmprel_size / sizeof(Elf64Rela), symtab, load_bias) != 0)
        return -1;

    return 0;
}

int elf_load_image(const void *data, size_t size, ElfImage *out)
{
    if (!data || !out || size < sizeof(Elf64Ehdr))
        return -1;

    const uint8_t *bytes = (const uint8_t *)data;
    const Elf64Ehdr *eh = (const Elf64Ehdr *)bytes;

    if (!elf_check_header(eh))
        return -1;

    if (eh->e_phoff + (uint64_t)eh->e_phnum * eh->e_phentsize > size)
    {
        kprintf("elf: program headers out of bounds\n");
        return -1;
    }

    const Elf64Phdr *phdrs = (const Elf64Phdr *)(bytes + eh->e_phoff);

    uint64_t lo = UINT64_MAX;
    uint64_t hi = 0;
    bool any_load = false;

    for (uint16_t i = 0; i < eh->e_phnum; i++)
    {
        const Elf64Phdr *ph = &phdrs[i];
        if (ph->p_type != PT_LOAD)
            continue;

        if (ph->p_offset + ph->p_filesz > size)
        {
            kprintf("elf: segment %d file range out of bounds\n", (int)i);
            return -1;
        }

        if (ph->p_memsz < ph->p_filesz)
        {
            kprintf("elf: segment %d memsz < filesz\n", (int)i);
            return -1;
        }

        any_load = true;
        if (ph->p_vaddr < lo)
            lo = ph->p_vaddr;
        uint64_t end = ph->p_vaddr + ph->p_memsz;
        if (end > hi)
            hi = end;
    }

    if (!any_load || hi <= lo)
    {
        kprintf("elf: no loadable segments\n");
        return -1;
    }

    uint64_t span = hi - lo;
    size_t page_count = (size_t)DIV_CEIL(span, PAGE_SIZE);

    void *image_base = pmm_alloc_pages(page_count);
    if (!image_base)
    {
        kprintf("elf: out of memory (%zu pages)\n", page_count);
        return -1;
    }

    memset(image_base, 0, page_count * PAGE_SIZE);

    uint64_t load_bias = (uint64_t)image_base - lo;

    for (uint16_t i = 0; i < eh->e_phnum; i++)
    {
        const Elf64Phdr *ph = &phdrs[i];
        if (ph->p_type != PT_LOAD)
            continue;

        uint8_t *dst = (uint8_t *)(ph->p_vaddr + load_bias);
        const uint8_t *src = bytes + ph->p_offset;

        memcpy(dst, src, ph->p_filesz);
    }

    if (eh->e_type == ET_DYN)
    {
        if (elf_apply_relocations(bytes, size, phdrs, eh->e_phnum, load_bias) != 0)
        {
            pmm_free_pages(image_base, page_count);
            return -1;
        }
    }

    out->base = image_base;
    out->page_count = page_count;
    out->entry = eh->e_entry + load_bias;
    out->load_bias = load_bias;

    return 0;
}

void elf_unload_image(ElfImage *img)
{
    if (!img || !img->base)
        return;

    pmm_free_pages(img->base, img->page_count);
    img->base = NULL;
    img->page_count = 0;
    img->entry = 0;
    img->load_bias = 0;
}
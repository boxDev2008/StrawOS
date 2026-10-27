#define ELF_MAG0 0x7F
#define ELF_MAG1 'E'
#define ELF_MAG2 'L'
#define ELF_MAG3 'F'

#define ELFCLASS64 2
#define ELFDATA2LSB 1
#define EM_X86_64 62
#define ET_EXEC 2
#define ET_DYN  3

#define PT_LOAD 1

typedef struct PACKED
{
    uint8_t  e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
}
Elf64Ehdr;

typedef struct PACKED
{
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
}
Elf64Phdr;

typedef struct
{
    void    *base;
    size_t   page_count;
    uint64_t entry;
    uint64_t load_bias;
}
ElfImage;

#define PT_DYNAMIC 2

#define DT_NULL    0
#define DT_PLTRELSZ 2
#define DT_SYMTAB  6
#define DT_RELA    7
#define DT_RELASZ  8
#define DT_RELAENT 9
#define DT_REL     17
#define DT_RELSZ   18
#define DT_RELENT  19
#define DT_JMPREL  23

#define R_X86_64_NONE      0
#define R_X86_64_64        1
#define R_X86_64_GLOB_DAT  6
#define R_X86_64_JUMP_SLOT 7
#define R_X86_64_RELATIVE  8

#define SHN_UNDEF 0

typedef struct
{
    int64_t  d_tag;
    uint64_t d_val;
}
Elf64Dyn;

typedef struct
{
    uint64_t r_offset;
    uint64_t r_info;
    int64_t  r_addend;
}
Elf64Rela;

typedef struct
{
    uint32_t st_name;
    uint8_t  st_info;
    uint8_t  st_other;
    uint16_t st_shndx;
    uint64_t st_value;
    uint64_t st_size;
}
Elf64Sym;

#define ELF64_R_TYPE(info) ((uint32_t)(info))
#define ELF64_R_SYM(info)  ((uint32_t)((info) >> 32))

int elf_load_image(const void *data, size_t size, ElfImage *out);
void elf_unload_image(ElfImage *img);
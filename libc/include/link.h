// SPDX-License-Identifier: MIT
#ifndef _LINK_H
#define _LINK_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint64_t Elf_Addr;
typedef uint16_t Elf_Half;

typedef struct {
    uint32_t p_type;
    uint32_t p_flags;
    Elf_Addr p_offset;
    Elf_Addr p_vaddr;
    Elf_Addr p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} Elf_Phdr;

#define PT_LOAD 1
#define PT_GNU_EH_FRAME 0x6474e550

struct dl_phdr_info {
    Elf_Addr dlpi_addr;
    const char *dlpi_name;
    const Elf_Phdr *dlpi_phdr;
    Elf_Half dlpi_phnum;
};

static inline int dl_iterate_phdr(int (*callback) (struct dl_phdr_info *info, size_t size, void *data), void *data) {
    (void)callback;
    (void)data;
    return 0;
}

#ifdef __cplusplus
}
#endif

#endif // _LINK_H

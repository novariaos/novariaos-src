#ifndef ELF_H
#define ELF_H

#include <log.h>
#include <types.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct {
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
} elf64_header_t;

typedef struct {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} elf64_phdr_t;

typedef struct {
    uint32_t sh_name;
    uint32_t sh_type;
    uint64_t sh_flags;
    uint64_t sh_addr;
    uint64_t sh_offset;
    uint64_t sh_size;
    uint32_t sh_link;
    uint32_t sh_info;
    uint64_t sh_addralign;
    uint64_t sh_entsize;
} elf64_shdr_t;

typedef struct {
    uint64_t entry_point;
    uint64_t text_start;
    uint64_t text_size;
    uint64_t data_start;
    uint64_t data_size;
    uint64_t bss_start;
    uint64_t bss_size;
} program_info_t;

typedef struct {
    uint64_t entry_point;
    uint64_t stack_top;
    uint64_t heap_start;
    uint64_t pid;
    bool is_running;
} process_info_t;

process_info_t* vfs_get_current_process(void);

#define EI_NIDENT 16
#define EI_CLASS 4
#define EI_DATA 5

#define ELFMAG0 0x7F
#define ELFMAG1 'E'
#define ELFMAG2 'L'
#define ELFMAG3 'F'

#define ELFCLASS64 2
#define ELFDATA2LSB 1

#define PT_LOAD 1
#define PT_DYNAMIC 2
#define PT_INTERP 3
#define PT_NOTE 4
#define PT_SHLIB 5
#define PT_PHDR 6

#define PF_X 0x1
#define PF_W 0x2
#define PF_R 0x4

#define SHT_PROGBITS 1
#define SHT_SYMTAB 2
#define SHT_STRTAB 3
#define SHT_NOBITS 8

bool elf_validate(const uint8_t* data, size_t size);
bool elf_is64bit(const uint8_t* data);
uint64_t elf_get_entry64(const uint8_t* data);
uint16_t elf_get_phnum64(const uint8_t* data);
elf64_phdr_t* elf_get_phdr64(const uint8_t* data, uint16_t index);
int elf_load_segment(void* dest, const uint8_t* elf_data, elf64_phdr_t* phdr);
int elf_load_program(void* dest, const uint8_t* elf_data, size_t elf_size);
int elf_load_program_relative(void* dest, uint64_t base_vaddr, const uint8_t* elf_data, size_t elf_size);

int elf_get_program_info(const uint8_t* elf_data, size_t elf_size, program_info_t* info);

int kmodule_load(const char* path);

#endif
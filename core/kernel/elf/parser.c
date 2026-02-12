#include <core/kernel/elf.h>
#include <core/kernel/mem.h>
#include <core/kernel/kstd.h>
#include <core/kernel/log.h>

bool elf_validate(const uint8_t* data, size_t size) {
    if (size < EI_NIDENT) return false;
    
    if (data[0] != ELFMAG0 || 
        data[1] != ELFMAG1 || 
        data[2] != ELFMAG2 || 
        data[3] != ELFMAG3) {
        return false;
    }

    if (data[EI_CLASS] != ELFCLASS64) {
        LOG_DEBUG("Unsupported ELF class: %d\n", data[EI_CLASS]);
        return false;
    }

    if (data[EI_DATA] != ELFDATA2LSB) {
        LOG_DEBUG("Unsupported endianness\n");
        return false;
    }
    
    return true;
}

bool elf_is64bit(const uint8_t* data) {
    return data[EI_CLASS] == ELFCLASS64;
}

uint64_t elf_get_entry64(const uint8_t* data) {
    elf64_header_t* ehdr = (elf64_header_t*)data;
    return ehdr->e_entry;
}

uint16_t elf_get_phnum64(const uint8_t* data) {
    elf64_header_t* ehdr = (elf64_header_t*)data;
    return ehdr->e_phnum;
}

elf64_phdr_t* elf_get_phdr64(const uint8_t* data, uint16_t index) {
    elf64_header_t* ehdr = (elf64_header_t*)data;
    
    if (index >= ehdr->e_phnum) {
        return NULL;
    }
    
    uint8_t* phdr_ptr = (uint8_t*)data + ehdr->e_phoff;
    phdr_ptr += index * ehdr->e_phentsize;
    
    return (elf64_phdr_t*)phdr_ptr;
}

int elf_load_segment(void* dest, const uint8_t* elf_data, elf64_phdr_t* phdr) {
    if (phdr->p_type != PT_LOAD) {
        return 0;
    }

    if (!(phdr->p_flags & PF_R)) {
        LOG_DEBUG("Segment not readable\n");
        return -1;
    }

    size_t file_size = phdr->p_filesz;
    size_t mem_size = phdr->p_memsz;
    
    if (file_size > mem_size) {
        LOG_DEBUG("Invalid segment sizes\n");
        return -1;
    }

    const uint8_t* segment_data = elf_data + phdr->p_offset;
    memcpy(dest, segment_data, file_size);

    if (mem_size > file_size) {
        uint8_t* bss_start = (uint8_t*)dest + file_size;
        size_t bss_size = mem_size - file_size;
        memset(bss_start, 0, bss_size);
    }
    
    return 0;
}

int elf_load_program(void* dest, const uint8_t* elf_data, size_t elf_size) {
    if (!elf_validate(elf_data, elf_size)) {
        LOG_DEBUG("Invalid ELF file\n");
        return -1;
    }

    if (!elf_is64bit(elf_data)) {
        LOG_DEBUG("Only 64-bit ELF supported\n");
        return -1;
    }

    uint64_t entry_point = elf_get_entry64(elf_data);
    LOG_DEBUG("Entry point: 0x%lx\n", entry_point);

    uint16_t phnum = elf_get_phnum64(elf_data);
    LOG_DEBUG("Program headers: %d\n", phnum);
    
    for (uint16_t i = 0; i < phnum; i++) {
        elf64_phdr_t* phdr = elf_get_phdr64(elf_data, i);
        if (!phdr) {
            LOG_DEBUG("Failed to get program header %d\n", i);
            return -1;
        }

        if (phdr->p_type == PT_LOAD) {
            LOG_DEBUG("Loading segment %d: offset=0x%lx, vaddr=0x%lx, size=0x%lx\n",
                        i, phdr->p_offset, phdr->p_vaddr, phdr->p_memsz);

            void* seg_dest = (void*)((uintptr_t)dest + phdr->p_vaddr);

            int result = elf_load_segment(seg_dest, elf_data, phdr);
            if (result < 0) {
                LOG_DEBUG("Failed to load segment %d\n", i);
                return -1;
            }
        }
    }
    
    return 0;
}

int elf_load_program_relative(void* dest, uint64_t base_vaddr, const uint8_t* elf_data, size_t elf_size) {
    if (!elf_validate(elf_data, elf_size)) {
        LOG_DEBUG("Invalid ELF file\n");
        return -1;
    }

    if (!elf_is64bit(elf_data)) {
        LOG_DEBUG("Only 64-bit ELF supported\n");
        return -1;
    }

    uint64_t entry_point = elf_get_entry64(elf_data);
    LOG_DEBUG("Entry point: 0x%lx (base_vaddr: 0x%lx)\n", entry_point, base_vaddr);

    uint16_t phnum = elf_get_phnum64(elf_data);
    LOG_DEBUG("Program headers: %d\n", phnum);

    for (uint16_t i = 0; i < phnum; i++) {
        elf64_phdr_t* phdr = elf_get_phdr64(elf_data, i);
        if (!phdr) {
            LOG_DEBUG("Failed to get program header %d\n", i);
            return -1;
        }

        if (phdr->p_type == PT_LOAD) {
            LOG_DEBUG("Loading segment %d: offset=0x%lx, vaddr=0x%lx, size=0x%lx\n",
                        i, phdr->p_offset, phdr->p_vaddr, phdr->p_memsz);

            uint64_t relative_vaddr = phdr->p_vaddr - base_vaddr;
            void* seg_dest = (void*)((uintptr_t)dest + relative_vaddr);

            LOG_DEBUG("Segment will be loaded at relative offset 0x%lx -> %p\n",
                     relative_vaddr, seg_dest);

            int result = elf_load_segment(seg_dest, elf_data, phdr);
            if (result < 0) {
                LOG_DEBUG("Failed to load segment %d\n", i);
                return -1;
            }
        }
    }

    return 0;
}

int elf_get_program_info(const uint8_t* elf_data, size_t elf_size, program_info_t* info) {
    if (!elf_validate(elf_data, elf_size)) {
        return -1;
    }
    
    if (!elf_is64bit(elf_data)) {
        return -1;
    }

    memset(info, 0, sizeof(program_info_t));
    info->entry_point = elf_get_entry64(elf_data);

    uint16_t phnum = elf_get_phnum64(elf_data);
    
    for (uint16_t i = 0; i < phnum; i++) {
        elf64_phdr_t* phdr = elf_get_phdr64(elf_data, i);
        if (!phdr || phdr->p_type != PT_LOAD) {
            continue;
        }

        if (phdr->p_flags & PF_X) {
            info->text_start = phdr->p_vaddr;
            info->text_size = phdr->p_memsz;
        }
        else if (phdr->p_flags & PF_W) {
            info->data_start = phdr->p_vaddr;
            info->data_size = phdr->p_filesz;
            info->bss_start = phdr->p_vaddr + phdr->p_filesz;
            info->bss_size = phdr->p_memsz - phdr->p_filesz;
        }
    }
    
    return 0;
}
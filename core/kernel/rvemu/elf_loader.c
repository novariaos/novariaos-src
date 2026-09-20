#include <core/kernel/rvemu/elf_loader.h>

#define ELF_HEADER_SIZE 64

#define PROGRAM_HEADER_SIZE 56
#define ELF_IDENT_CLASS_OFFSET 4
#define ELF_IDENT_DATA_OFFSET 5
#define ELF_TYPE_OFFSET 16
#define ELF_MACHINE_OFFSET 18
#define ELF_ENTRY_OFFSET 24
#define ELF_PHOFF_OFFSET 32
#define ELF_PHENTSIZE_OFFSET 54
#define ELF_PHNUM_OFFSET 56

#define ELF_CLASS_64 2
#define ELF_DATA_LITTLE_ENDIAN 1
#define ELF_TYPE_EXEC 2
#define ELF_MACHINE_RISCV 0xF3

#define PH_TYPE_OFFSET 0
#define PH_OFFSET_OFFSET 8
#define PH_VADDR_OFFSET 16
#define PH_FILESZ_OFFSET 32
#define PH_MEMSZ_OFFSET 40

#define PH_TYPE_LOAD 1

static uint32_t read_le_uint16(const uint8_t *image, uint64_t offset) {
    return (uint32_t)image[offset]
         | ((uint32_t)image[offset + 1] << 8);
}

static uint32_t read_le_uint32(const uint8_t *image, uint64_t offset) {
    return (uint32_t)image[offset]
         | ((uint32_t)image[offset + 1] << 8)
         | ((uint32_t)image[offset + 2] << 16)
         | ((uint32_t)image[offset + 3] << 24);
}

static uint64_t read_le_uint64(const uint8_t *image, uint64_t offset) {
    return (uint64_t)image[offset]
         | ((uint64_t)image[offset + 1] << 8)
         | ((uint64_t)image[offset + 2] << 16)
         | ((uint64_t)image[offset + 3] << 24)
         | ((uint64_t)image[offset + 4] << 32)
         | ((uint64_t)image[offset + 5] << 40)
         | ((uint64_t)image[offset + 6] << 48)
         | ((uint64_t)image[offset + 7] << 56);
}

int elf_loader_load(const uint8_t *image, uint64_t image_size,
                    rv64_memory_t *memory, uint64_t *entry_point) {
    *entry_point = 0;

    if (image_size < ELF_HEADER_SIZE) {
        return 1;
    }

    if (image[0] != 0x7F || image[1] != 'E' || image[2] != 'L' || image[3] != 'F') {
        return 1;
    }
    if (image[ELF_IDENT_CLASS_OFFSET] != ELF_CLASS_64) {
        return 1;
    }
    if (image[ELF_IDENT_DATA_OFFSET] != ELF_DATA_LITTLE_ENDIAN) {
        return 1;
    }
    if (read_le_uint16(image, ELF_TYPE_OFFSET) != ELF_TYPE_EXEC) {
        return 1;
    }
    if (read_le_uint16(image, ELF_MACHINE_OFFSET) != ELF_MACHINE_RISCV) {
        return 1;
    }

    uint64_t entry = read_le_uint64(image, ELF_ENTRY_OFFSET);
    uint64_t ph_offset = read_le_uint64(image, ELF_PHOFF_OFFSET);
    uint64_t ph_entry_size = (uint64_t)read_le_uint16(image, ELF_PHENTSIZE_OFFSET);
    uint64_t ph_count = (uint64_t)read_le_uint16(image, ELF_PHNUM_OFFSET);

    for (uint64_t index = 0; index < ph_count; index++) {
        uint64_t ph_position = ph_offset + index * ph_entry_size;
        if (ph_position > image_size) {
            return 1;
        }
        if (ph_entry_size < PROGRAM_HEADER_SIZE) {
            return 1;
        }
        if (ph_position + PROGRAM_HEADER_SIZE > image_size) {
            return 1;
        }

        uint32_t ph_type = read_le_uint32(image, ph_position + PH_TYPE_OFFSET);
        if (ph_type != PH_TYPE_LOAD) {
            continue;
        }

        uint64_t segment_offset = read_le_uint64(image, ph_position + PH_OFFSET_OFFSET);
        uint64_t segment_vaddr = read_le_uint64(image, ph_position + PH_VADDR_OFFSET);
        uint64_t segment_file_size = read_le_uint64(image, ph_position + PH_FILESZ_OFFSET);
        uint64_t segment_mem_size = read_le_uint64(image, ph_position + PH_MEMSZ_OFFSET);

        if (segment_offset > image_size) {
            return 1;
        }
        if (segment_file_size > image_size - segment_offset) {
            return 1;
        }
        if (!memory_contains(memory, segment_vaddr, segment_mem_size)) {
            return 1;
        }

        if (segment_file_size > 0) {
            if (memory_copy_in(memory, segment_vaddr, image + segment_offset, segment_file_size) != 0) {
                return 1;
            }
        }
        if (segment_mem_size > segment_file_size) {
            if (memory_zero_range(memory, segment_vaddr + segment_file_size,
                                  segment_mem_size - segment_file_size) != 0) {
                return 1;
            }
        }
    }

    *entry_point = entry;
    return 0;
}
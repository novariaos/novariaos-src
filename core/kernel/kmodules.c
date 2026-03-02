#include <core/kernel/elf.h>
#include <core/kernel/mem.h>
#include <core/kernel/kstd.h>
#include <core/kernel/log.h>
#include <core/fs/vfs.h>
#include <core/kernel/vge/fb_render.h>
#include <core/drivers/keyboard.h>

struct kernel_api {
    void (*kprint)(const char *str, int color);
    int (*vfs_pseudo_register)(const char* filename, vfs_dev_read_t read_fn, vfs_dev_write_t write_fn, vfs_dev_seek_t seek_fn, vfs_dev_ioctl_t ioctl_fn, void* dev_data);
    void* (*kmalloc)(size_t size);
    void (*kfree)(void* ptr);
    int (*keyboard_register_hotkey)(int scancode, int modifiers, void (*callback)(void*), void* data);
    void (*keyboard_unregister_hotkey)(int id);
    uint32_t* (*get_framebuffer)(void);
    void (*get_fb_dimensions)(uint32_t* width, uint32_t* height, uint32_t* pitch);
    uint32_t (*get_fb_pitch_pixels)(void);
};

int kmodule_load(const char* path) {
    if (!vfs_exists(path)) {
        LOG_DEBUG("Module not found: %s\n", path);
        return -1;
    }

    int fd = vfs_open(path, VFS_READ);
    if (fd < 0) {
        LOG_DEBUG("Failed to open module: %s\n", path);
        return -2;
    }

    vfs_off_t file_size = vfs_seek(fd, 0, VFS_SEEK_END);
    vfs_seek(fd, 0, VFS_SEEK_SET);
    
    if (file_size <= 0) {
        vfs_close(fd);
        return -3;
    }

    uint8_t* elf_data = kmalloc(file_size);
    if (!elf_data) {
        vfs_close(fd);
        return -4;
    }
    
    vfs_readfd(fd, elf_data, file_size);
    vfs_close(fd);

    if (!elf_validate(elf_data, file_size)) {
        LOG_DEBUG("Invalid ELF file\n");
        kfree(elf_data);
        return -5;
    }

    program_info_t prog_info;
    if (elf_get_program_info(elf_data, file_size, &prog_info) < 0) {
        LOG_DEBUG("Failed to get program info\n");
        kfree(elf_data);
        return -6;
    }

    uint16_t phnum = elf_get_phnum64(elf_data);
    uint64_t min_vaddr = UINT64_MAX;
    uint64_t max_vaddr = 0;
    
    for (uint16_t i = 0; i < phnum; i++) {
        elf64_phdr_t* phdr = elf_get_phdr64(elf_data, i);
        if (phdr && phdr->p_type == PT_LOAD) {
            if (phdr->p_vaddr < min_vaddr) min_vaddr = phdr->p_vaddr;
            uint64_t end = phdr->p_vaddr + phdr->p_memsz;
            if (end > max_vaddr) max_vaddr = end;
        }
    }
    
    if (min_vaddr == UINT64_MAX || max_vaddr == 0) {
        LOG_DEBUG("No loadable segments\n");
        kfree(elf_data);
        return -7;
    }

    uint64_t total_size = max_vaddr - min_vaddr;
    total_size = (total_size + 0xFFF) & ~0xFFF;
    
    void* load_addr = kmalloc(total_size + 0x1000); 
    if (!load_addr) {
        kfree(elf_data);
        return -8;
    }

    uintptr_t aligned_addr = ((uintptr_t)load_addr + 0xFFF) & ~0xFFF;
    load_addr = (void*)aligned_addr;

    memset(load_addr, 0, total_size);

    if (elf_load_program_relative(load_addr, min_vaddr, elf_data, file_size) < 0) {
        LOG_DEBUG("Failed to load ELF segments\n");
        kfree(elf_data);
        kfree(load_addr);
        return -9;
    }
    
    kfree(elf_data);

    uint64_t entry_point = prog_info.entry_point - min_vaddr + (uint64_t)load_addr;
    
    LOG_DEBUG("Module loaded at 0x%p, entry at 0x%lx\n", load_addr, entry_point);
    LOG_DEBUG("Calling module init...\n");

    struct kernel_api api;
    api.kprint = kprint;
    api.vfs_pseudo_register = vfs_pseudo_register;
    api.kmalloc = kmalloc;
    api.kfree = kfree;
    api.keyboard_register_hotkey = keyboard_register_hotkey;
    api.keyboard_unregister_hotkey = keyboard_unregister_hotkey;
    api.get_framebuffer = get_framebuffer;
    api.get_fb_dimensions = get_fb_dimensions;
    api.get_fb_pitch_pixels = get_fb_pitch_pixels;

    void (*module_entry)(struct kernel_api*) __attribute__((force_align_arg_pointer));
    module_entry = (void*)entry_point;
    
    module_entry(&api);
    
    LOG_DEBUG("Module returned to kernel\n");
    return 0;
}
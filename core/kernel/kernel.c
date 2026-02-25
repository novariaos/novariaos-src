// SPDX-License-Identifier: GPL-3.0-only

#include <core/kernel/kstd.h>
#include <core/kernel/mem.h>
#include <core/kernel/nvm/nvm.h>
#include <core/kernel/nvm/caps.h>
#include <core/drivers/serial.h>
#include <core/kernel/vge/fb.h>
#include <core/kernel/vge/palette.h>
#include <core/drivers/timer.h>
#include <core/drivers/keyboard.h>
#include <core/drivers/cdrom.h>
#include <core/drivers/ramdisk.h>
#include <core/kernel/shell.h>
#include <core/kernel/log.h>
#include <core/fs/ramfs.h>
#include <core/fs/initramfs.h>
#include <core/fs/iso9660.h>
#include <core/fs/vfs.h>
#include <core/fs/block_dev_vfs.h>
#include <core/fs/block.h>
#include <core/fs/fat32.h>
#include <rootfs/usr/src/userspace_init.h>
#include <stddef.h>
#include <stdbool.h>
#include <lib/limine.h>
#include <core/kernel/elf.h>

static volatile struct limine_module_request module_request = {
    .id = { LIMINE_COMMON_MAGIC, 0x3e7e279702be32af, 0xca1c4f3bd1280cee },
    .revision = 0
};

static volatile struct limine_rsdp_request rsdp_request = {
    .id = { LIMINE_COMMON_MAGIC, 0xc5e77b6b397e7b43, 0x27637845accdcf3c },
    .revision = 0
};

static volatile struct limine_bootloader_info_request bootloader_info_request = {
    .id = { LIMINE_COMMON_MAGIC, 0xf55038d8e2a1202f, 0x279426fcf5f59740 },
    .revision = 0
};

static volatile struct limine_executable_address_request kernel_address_request = {
    .id = { LIMINE_COMMON_MAGIC, 0x71ba76863cc55f63, 0xb2644a48c516a487 },
    .revision = 0
};

static volatile struct limine_mp_request smp_request = {
    .id = { LIMINE_COMMON_MAGIC, 0x95a67b819a1b857e, 0xa0b61b723b6a73e0 },
    .revision = 0,
    .flags = 0
};

static volatile struct limine_paging_mode_request paging_mode_request = {
    .id = { LIMINE_COMMON_MAGIC, 0x95c1a0edab0944cb, 0xa4e5cb3842f7488a },
    .revision = 0
};

void limine_smp_entry(struct limine_mp_info *info) {
    // This function is called on each additional CPU
    // For now, just halt the CPU
    while (1) {
        asm volatile("hlt");
    }
}

void kmain() {
    init_fb();
    kprint(":: Initializing memory manager...\n", 7);
    initializeMemoryManager();

    init_serial();
    ramfs_init();
    vfs_init();
    block_init();
    fat32_init();
    syslog_init();
    keyboard_init();
    
    void* iso_location = NULL;
    size_t iso_size = 0;

    void* initramfs_location = NULL;
    size_t initramfs_size = 0;

    if (module_request.response != NULL && module_request.response->module_count > 0) {
        LOG_DEBUG("Checking Limine modules...\n");

        int disk_index = 0;
        for (uint64_t i = 0; i < module_request.response->module_count; i++) {
            struct limine_file *module = module_request.response->modules[i];
            LOG_DEBUG("Module %d: size=%d\n", i, module->size);

            // Check for ISO9660 filesystem
            if (module->size > 0x8005) {
                char* sig = (char*)module->address + 0x8001;
                if (sig[0] == 'C' && sig[1] == 'D' && sig[2] == '0' &&
                    sig[3] == '0' && sig[4] == '1') {
                    iso_location = (void*)module->address;
                    iso_size = module->size;
                    LOG_DEBUG("Found ISO9660 in module %d\n", i);
                    continue;
                }
            }

            // Check for disk image by MBR signature (0x55AA at offset 510)
            if (module->size >= 512) {
                uint8_t* mbr = (uint8_t*)module->address;
                if (mbr[510] == 0x55 && mbr[511] == 0xAA) {
                    char name[4] = {'h', 'd', 'a' + disk_index, '\0'};
                    ramdisk_register(name, (void*)module->address, module->size);
                    LOG_DEBUG("Found disk image in module %d, registered as %s\n", i, name);
                    disk_index++;
                    continue;
                }
            }

            // Check for initramfs (not ISO and reasonable size)
            if (!initramfs_location && module->size > 0) {
                initramfs_location = (void*)module->address;
                initramfs_size = module->size;
                LOG_DEBUG("Found initramfs in module %d, size=%d\n", i, initramfs_size);
            }
        }
    }
    
    if (iso_location) {
        cdrom_set_iso_data(iso_location, iso_size);
        cdrom_init();
        iso9660_init(iso_location, iso_size);
        LOG_DEBUG("ISO9660 filesystem mounted\n");

        iso9660_mount_to_vfs("/", "/");
        LOG_DEBUG("ISO contents mounted to /\n");

        LOG_DEBUG("Checking mounted files...\n");
        vfs_list();
 
        init_vge_font();
        palette_init();
        clear_screen();
    } else {
        LOG_DEBUG(":: ISO9660 filesystem not found\n");
    }

    // Register any discovered block devices with the VFS.
    block_dev_vfs_init();
    
    const char* ascii_art[] = {
        " _   _                      _        ___  ____  ",
        "| \\ | | _____   ____ _ _ __(_) __ _ / _ \\/ ___| ",
        "|  \\| |/ _ \\ \\ / / _` | '__| |/ _` | | | \\___ \\ ",
        "| |\\  | (_) \\ V / (_| | |  | | (_| | |_| |___) |",
        "|_| \\_|\\___/ \\_/ \\__,_|_|  |_|\\__,_|\\___/|____/ "
    };

    for (int i = 0; i < sizeof(ascii_art) / sizeof(ascii_art[0]); i++) {
        kprint(ascii_art[i], 15);
        kprint("\n", 15);
    }

    kprint("                                 TG: ", 15);
    kprint("@NovariaOS\n", 9);

    if (initramfs_location) {
        LOG_DEBUG("Loading initramfs at 0x%p, size=%d\n", initramfs_location, initramfs_size);
        initramfs_load_from_memory(initramfs_location, initramfs_size);
    } else {
        LOG_DEBUG("Initramfs not found\n");
    }
    LOG_DEBUG("Initramfs loaded\n");
    nvm_init();
    LOG_DEBUG("NVM initialized\n");
    LOG_DEBUG("Userspace programs registered\n");

    size_t program_count = initramfs_get_count();
    LOG_DEBUG("Initramfs program count: %d\n", program_count);
    if (program_count > 0) {
        for (size_t i = 0; i < program_count; i++) {
            struct program* prog = initramfs_get_program(i);
            if (prog && prog->size > 0) {
                char buf[32];
                int n = i;
                char* p = buf;
                if (n == 0) {
                    *p++ = '0';
                } else {
                    char* start = p;
                    while (n > 0) {
                        *p++ = '0' + n % 10;
                        n /= 10;
                    }
                    p--;
                    while (start < p) {
                        char temp = *start;
                        *start = *p;
                        *p = temp;
                        start++;
                        p--;
                    }
                }
                *p = '\0';
                
                nvm_execute((uint8_t*)prog->data, prog->size, (uint16_t[]){CAP_ALL}, 1);
            }
        }
    }

    vfs_dirent_t entries[32];
    int num = vfs_readdir("/boot/modules", entries, 32);
    if (num > 0) {
        char path[256];
        for (int i = 0; i < num; i++) {
            const char* name = entries[i].d_name;
            size_t len = strlen(name);
            if (len > 3 && strcmp(name + len - 3, ".ko") == 0) {
                char* p = path;
                const char* base = "/boot/modules/";
                while (*base) *p++ = *base++;
                const char* n = name;
                while (*n) *p++ = *n++;
                *p = '\0';
                
                kmodule_load(path);
            }
        }
    }

    shell_init();
    shell_run();

    while(true) {
        keyboard_getchar();
        nvm_scheduler_tick();
    }
}
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
#include <core/drivers/ide.h>
#include <core/drivers/nvme.h>
#include <core/drivers/ahci.h>
#include <core/kernel/shell.h>
#include <log.h>
#include <core/fs/iso9660.h>
#include <core/fs/vfs.h>
#include <core/fs/block_dev_vfs.h>
#include <core/fs/block.h>
#include <core/fs/fat32.h>
#include <core/fs/ext2.h>
#include <stddef.h>
#include <stdbool.h>
#include <limine.h>
#include <core/kernel/elf.h>
#include <core/arch/idt.h>
#include <core/arch/smp.h>
#include <core/arch/work_queue.h>
#include <core/kernel/mem/slab.h>
#include <core/kernel/mem/cpu_pool.h>
#include <core/arch/panic.h>
#include <core/arch/idt.h>
#include <core/arch/rtc.h>

// Limine requests
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

static void show_banner(void) {
    const char* ascii_art[] = {
        " _   _                      _        ___  ____  ",
        "| \\ | | _____   ____ _ _ __(_) __ _ / _ \\/ ___| ",
        "|  \\| |/ _ \\ \\ / / _` | '__| |/ _` | | | \\___ \\ ",
        "| |\\  | (_) \\ V / (_| | |  | | (_| | |_| |___) |",
        "|_| \\_|\\___/ \\_/ \\__,_|_|  |_|\\__,_|\\___/|____/ "
    };
    
    fb_set_font_loaded();
    for (int i = 0; i < sizeof(ascii_art) / sizeof(ascii_art[0]); i++) {
        kprint(ascii_art[i], 15);
        kprint("\n", 15);
    }

    kprint("                                 TG: ", 15);
    kprint("@NovariaOS\n", 9);
}

static void init_video(void) {
    init_fb();
    clear_screen();
    kprint(":: Video subsystem initialized\n", 7);
}

static void init_serial_port(void) {
    init_serial();
    kprint(":: Serial port initialized\n", 7);
}

static void init_memory(void) {
    kprint(":: Initializing memory manager...\n", 7);
    memory_manager_init();
    kprint(":: Memory manager ready\n", 7);
}

static void init_interrupts(void) {
    idt_init();
    kprint(":: Interrupt descriptor table initialized\n", 7);
}

static void init_syslog(void) {
    syslog_init();
    kprint(":: System logger initialized\n", 7);
}

static void init_keyboard(void) {
    keyboard_init();
    kprint(":: Keyboard controller initialized\n", 7);
}

static void init_nvm(void) {
    nvm_init();
    kprint(":: NVM subsystem initialized\n", 7);
}

static void init_multiprocessing(void) {
    smp_init(smp_request.response);
    kprint(":: SMP initialized\n", 7);
    
    wq_init();
    kprint(":: Work queue initialized\n", 7);
    
    slab_cpu_init(0);
    kprint(":: Slab allocator initialized\n", 7);
    
    cpu_pool_init(0);
    kprint(":: CPU pool initialized\n", 7);
}


static void init_filesystems(void) {
    vfs_init();
    kprint(":: Virtual filesystem initialized\n", 7);
    
    block_init();
    kprint(":: Block subsystem initialized\n", 7);
    
    ide_init();
    kprint(":: IDE controller initialized\n", 7);
    
    nvme_init();
    kprint(":: NVMe controller initialized\n", 7);
    
    ahci_init();
    kprint(":: AHCI controller initialized\n", 7);
    
    fat32_init();
    kprint(":: FAT32 filesystem initialized\n", 7);
    
    ext2_init();
    kprint(":: Ext2 filesystem initialized\n", 7);
    
    block_dev_vfs_init();
    kprint(":: Block device VFS layer initialized\n", 7);
}

static void init_boot_modules(void) {
    if (module_request.response == NULL || module_request.response->module_count == 0) {
        LOG_DEBUG("No boot modules found\n");
        return;
    }

    kprint(":: Processing boot modules...\n", 7);
    LOG_DEBUG("Processing %d boot modules...\n", module_request.response->module_count);

    int disk_index = 0;
    void* iso_location = NULL;
    size_t iso_size = 0;

    for (uint64_t i = 0; i < module_request.response->module_count; i++) {
        struct limine_file *module = module_request.response->modules[i];
        LOG_DEBUG("Module %d: size=%d\n", i, module->size);

        // Check for ISO9660
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

        // Check for disk image (MBR signature)
        if (module->size >= 512) {
            uint8_t* mbr = (uint8_t*)module->address;
            if (mbr[510] == 0x55 && mbr[511] == 0xAA) {
                char name[4] = {'h', 'd', 'a' + disk_index, '\0'};
                ramdisk_register(name, (void*)module->address, module->size);
                kprint(":: Registered boot module as ", 7);
                kprint(name, 9);
                kprint("\n", 7);
                disk_index++;
            }
        }
    }

    if (iso_location) {
        cdrom_set_iso_data(iso_location, iso_size);
        cdrom_init();
        kprint(":: CD-ROM initialized from boot module\n", 7);
        
        iso9660_init(iso_location, iso_size);
        kprint(":: ISO9660 filesystem initialized\n", 7);
        
        iso9660_mount_to_vfs("/", "/");
        kprint(":: Root filesystem mounted from rootfs.img (ISO9660)\n", 7);

        vfs_list();
        init_vge_font();
        palette_init();
        clear_screen();
    } else {
        LOG_DEBUG("ISO9660 filesystem not found\n");
    }
}

static void load_kernel_modules(void) {
    vfs_dirent_t entries[32];
    int num = vfs_readdir("/boot/modules", entries, 32);
    if (num > 0) {
        char path[256];
        int loaded = 0;
        
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

                if (kmodule_load(path) == 0) {
                    loaded++;
                }
            }
        }
        
        if (loaded > 0) {
            kprint(":: Loaded ", 7);
            char count_str[16];
            itoa(loaded, count_str, 10);
            kprint(count_str, 9);
            kprint(" kernel module(s)\n", 7);
        }
    } else {
        kprint(":: No kernel modules found\n", 7);
    }
}

void limine_smp_entry(struct limine_mp_info *info) {
    // This function is called on each additional CPU
    // For now, just halt the CPU
    while (1) {
        asm volatile("hlt");
    }
}


void kmain() {
    // Early initialization
    init_serial_port();
    init_video();
    
    // Core system initialization
    init_memory();
    init_interrupts();
    init_syslog();
    init_keyboard();
    init_nvm();
    init_multiprocessing();
    init_timer_subsystem();
    
    // Filesystem initialization
    init_filesystems();
    
    // Boot modules and root filesystem
    init_boot_modules();

    // Display banner
    show_banner();

    load_kernel_modules();

    shell_init();
    shell_run();
    
    // Work loop
    while(true) {
        keyboard_getchar();
        nvm_scheduler_tick();
    }
}
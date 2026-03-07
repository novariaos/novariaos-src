// SPDX-License-Identifier: GPL-3.0-only

#include <core/kernel/userspace.h>
#include <core/kernel/kstd.h>
#include <core/kernel/vge/fb_render.h>
#include <stddef.h>
#include <string.h>

#define MAX_PROGRAMS 32
#define MAX_PROGRAM_NAME 16

typedef struct {
    char name[MAX_PROGRAM_NAME];
    userspace_program_t program;
    bool used;
} userspace_entry_t;

static userspace_entry_t programs[MAX_PROGRAMS];

// Initialize userspace
static void userspace_init(void) {
    static bool initialized = false;
    if (initialized) return;
    
    for (int i = 0; i < MAX_PROGRAMS; i++) {
        programs[i].used = false;
        programs[i].name[0] = '\0';
        programs[i].program = NULL;
    }
    
    initialized = true;
}

// Register a userspace program
void userspace_register(const char* name, userspace_program_t program) {
    userspace_init();
    
    // Find free slot
    for (int i = 0; i < MAX_PROGRAMS; i++) {
        if (!programs[i].used) {
            strcpy(programs[i].name, name);
            programs[i].program = program;
            programs[i].used = true;
            return;
        }
    }
}

// Execute a userspace program
int userspace_exec(const char* name, int argc, char** argv) {
    userspace_init();
    
    for (int i = 0; i < MAX_PROGRAMS; i++) {
        if (programs[i].used && strcmp(programs[i].name, name) == 0) {
            return programs[i].program(argc, argv);
        }
    }
    
    return -1; // Program not found
}

// Check if program exists
bool userspace_exists(const char* name) {
    userspace_init();
    
    for (int i = 0; i < MAX_PROGRAMS; i++) {
        if (programs[i].used && strcmp(programs[i].name, name) == 0) {
            return true;
        }
    }
    
    return false;
}

// List all registered programs
void userspace_list(void) {
    userspace_init();
    
    int count = 0;
    for (int i = 0; i < MAX_PROGRAMS; i++) {
        if (programs[i].used) {
            count++;
        }
    }
    
    if (count == 0) {
        kprint("No programs registered\n", 7);
        return;
    }
    
    char buf[16];
    kprint("Registered programs: ", 7);
    itoa(count, buf, 10);
    kprint(buf, 7);
    kprint("\n", 7);
    
    for (int i = 0; i < MAX_PROGRAMS; i++) {
        if (programs[i].used) {
            kprint("  ", 7);
            kprint(programs[i].name, 11);
            kprint("\n", 7);
        }
    }
}

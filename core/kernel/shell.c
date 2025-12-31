// SPDX-License-Identifier: GPL-3.0-only

#include <core/kernel/shell.h>
#include <core/kernel/kstd.h>
#include <core/drivers/keyboard.h>
#include <core/kernel/vge/fb.h>
#include <core/kernel/mem.h>
#include <core/fs/initramfs.h>
#include <core/fs/iso9660.h>
#include <core/fs/vfs.h>
#include <core/kernel/nvm/nvm.h>
#include <core/kernel/nvm/caps.h>
#include <core/kernel/userspace.h>

#define MAX_COMMAND_LENGTH 256
#define MAX_PATH_LENGTH 64

static char current_working_directory[MAX_PATH_LENGTH] = "/";

static void cmd_help(void) {
    kprint("Built-in commands:\n", 7);
    kprint("  help     - Show this help message\n", 7);
    kprint("  pwd      - Print working directory\n", 7);
    kprint("  ls       - List directory contents\n", 7);
    kprint("  cat      - Display file contents\n", 7);
    kprint("  cd       - Change directory\n", 7);
    kprint("\n", 7);
}

static void cmd_cat(const char* args) {
    const char* path = args;
    while (*path == ' ') path++;
    
    if (*path == '\0') {
        kprint("cat: Usage: cat <filename>\n", 7);
        return;
    }
    
    char full_path[MAX_PATH_LENGTH];
    
    if (path[0] == '/') {
        strcpy(full_path, path);
    } else {
        strcpy(full_path, current_working_directory);
        if (full_path[strlen(full_path)-1] != '/') {
            strcat(full_path, "/");
        }
        strcat(full_path, path);
    }

    size_t size;
    const char* data = vfs_read(full_path, &size);

    if (data == NULL) {
        kprint("cat: ", 7);
        kprint(path, 7);
        kprint(": No such file or directory\n", 7);
        return;
    }

    for (size_t i = 0; i < size; i++) {
        char c[2] = {data[i], '\0'};
        if (data[i] == '\n') {
            kprint("\n", 7);
        } else {
            kprint(c, 15);
        }
    }
    kprint("\n", 7);
}

static void cmd_ls(const char* args) {
    const char* path = args;
    while (*path == ' ') path++;

    if (*path == '\0') {
        path = current_working_directory;
    }

    vfs_file_t* files = vfs_get_files();
    int dir_len = strlen(path);

    char normalized_dir[64];
    size_t i = 0;
    while (path[i] && i < 63) {
        normalized_dir[i] = path[i];
        i++;
    }
    normalized_dir[i] = '\0';

    if (dir_len > 1 && normalized_dir[dir_len - 1] == '/') {
        normalized_dir[dir_len - 1] = '\0';
        dir_len--;
    }

    int found_count = 0;

    for (size_t i = 0; i < 128; i++) {
        if (!files[i].used) continue;

        const char* name = files[i].name;
        bool should_show = false;
        const char* display_name = name;

        if (dir_len == 1 && normalized_dir[0] == '/') {
            if (name[0] == '/' && name[1] != '\0') {
                int slash_count = 0;
                for (int j = 1; name[j] != '\0'; j++) {
                    if (name[j] == '/') slash_count++;
                }
                if (slash_count == 0) {
                    should_show = true;
                    display_name = name + 1;
                }
            }
        } else {
            size_t name_len = strlen(name);

            if (name_len > dir_len && name[dir_len] == '/') {
                bool match = true;
                for (size_t j = 0; j < dir_len; j++) {
                    if (name[j] != normalized_dir[j]) {
                        match = false;
                        break;
                    }
                }
                if (match) {
                    size_t slash_count = 0;
                    for (size_t j = dir_len + 1; name[j] != '\0'; j++) {
                        if (name[j] == '/') slash_count++;
                    }
                    if (slash_count == 0) {
                        should_show = true;
                        display_name = name + dir_len + 1;
                    }
                }
            }
        }

        if (should_show) {
            found_count++;
            if (files[i].type == VFS_TYPE_DIR) {
                kprint(display_name, 9);
                kprint("/", 9);
            } else {
                kprint(display_name, 7);
            }
            kprint("    ", 7);
        }
    }
    kprint("\n", 7);
}

static int parse_command(const char* command, char* argv[], int max_args) {
    int argc = 0;
    static char cmd_buf[MAX_COMMAND_LENGTH];
    
    int i = 0;
    while (command[i] && i < MAX_COMMAND_LENGTH - 1) {
        cmd_buf[i] = command[i];
        i++;
    }
    cmd_buf[i] = '\0';
    
    char* p = cmd_buf;
    while (*p && argc < max_args) {
        while (*p == ' ') p++;
        
        if (*p == '\0') break;
        
        argv[argc++] = p;
        
        while (*p && *p != ' ') p++;
        
        if (*p) {
            *p = '\0';
            p++;
        }
    }
    
    return argc;
}

static int should_delay_prompt = 0;
static int delay_ticks = 0;

static int create_spawn_stack(char* argv[], int argc, int32_t** stack_out) {
    if (argc == 0) return 0;

    int total_size = 0;
    for (int i = 0; i < argc; i++) {
        total_size += strlen(argv[i]) + 1;
    }
    total_size += 1;

    int32_t* stack = kmalloc(total_size * sizeof(int32_t));
    if (!stack) return -1;

    int pos = 0;

    for (int i = argc - 1; i >= 0; i--) {
        char* arg = argv[i];
        for (int j = 0; arg[j] != '\0'; j++) {
            stack[pos++] = (int32_t)(uint8_t)arg[j];
        }
        stack[pos++] = 0;
    }

    stack[pos++] = argc;
    *stack_out = stack;
    return pos;
}

static void execute_command(const char* command) {
    while (*command == ' ') command++;
    
    if (*command == '\0') {
        return;
    }
    
    char* argv[16];
    int argc = parse_command(command, argv, 16);
    
    if (argc == 0) return;
    
    if (strcmp(argv[0], "help") == 0) {
        cmd_help();
    } else if (strcmp(argv[0], "pwd") == 0) {
        kprint(current_working_directory, 11);
        kprint("\n", 7);
    } else if (strcmp(argv[0], "ls") == 0) {
        if (argc > 1) {
            cmd_ls(argv[1]);
        } else {
            cmd_ls("");
        }
    } else if (strcmp(argv[0], "cat") == 0) {
        if (argc > 1) {
            cmd_cat(argv[1]);
        } else {
            kprint("cat: Usage: cat <filename>\n", 7);
        }
    } else if (strcmp(argv[0], "cd") == 0) {
        shell_set_cwd(argv[1]);
    }  else {
        char bin_path[64];
        int len = strlen(argv[0]);
        
        bin_path[0] = '/';
        bin_path[1] = 'b';
        bin_path[2] = 'i';
        bin_path[3] = 'n';
        bin_path[4] = '/';
        
        for (size_t i = 0; i < len && i < 58; i++) {
            bin_path[5 + i] = argv[0][i];
        }
        
        bin_path[5 + len] = '.';
        bin_path[6 + len] = 'b';
        bin_path[7 + len] = 'i';
        bin_path[8 + len] = 'n';
        bin_path[9 + len] = '\0';
        
        if (vfs_exists(bin_path)) {
            size_t size;
            const char* data = vfs_read(bin_path, &size);
            
            if (data && size > 0) {
                should_delay_prompt = 1;
                delay_ticks = 50;
                nvm_execute((uint8_t*)data, size, (uint16_t[]){CAP_ALL}, 1);
                return;
            } else {
                kprint("Error: Failed to read program file\n", 12);
            }
        } else if (userspace_exists(argv[0])) {
            int ret = userspace_exec(argv[0], argc, argv);
            if (ret != 0) {
                kprint("\nProgram exited with code ", 12);
                char buf[16];
                itoa(ret, buf, 10);
                kprint(buf, 12);
                kprint("\n", 12);
            }
        } else {
            kprint(argv[0], 7);
            kprint(": command not found\n", 7);
        }
    }
}

const char* shell_get_cwd(void) {
    return current_working_directory;
}

static void normalize_path(const char* input_path, char* output) {
    char temp[MAX_PATH_LENGTH];
    char* parts[32];
    int part_count = 0;
    int temp_idx = 0;
    
    if (input_path[0] == '/') {
        temp[0] = '/';
        temp_idx = 1;
        const char* p = input_path + 1;
        while (*p && temp_idx < MAX_PATH_LENGTH - 1) {
            temp[temp_idx++] = *p++;
        }
    } else {
        const char* cwd = current_working_directory;
        while (*cwd && temp_idx < MAX_PATH_LENGTH - 1) {
            temp[temp_idx++] = *cwd++;
        }
        if (temp_idx > 0 && temp[temp_idx-1] != '/') {
            temp[temp_idx++] = '/';
        }
        const char* p = input_path;
        while (*p && temp_idx < MAX_PATH_LENGTH - 1) {
            temp[temp_idx++] = *p++;
        }
    }
    temp[temp_idx] = '\0';
    
    int i = 0;
    while (i < temp_idx) {
        while (i < temp_idx && temp[i] == '/') i++;
        
        if (i >= temp_idx) break;
        
        int start = i;
        while (i < temp_idx && temp[i] != '/') i++;
        
        int len = i - start;
        if (len == 1 && temp[start] == '.') {
            continue;
        } else if (len == 2 && temp[start] == '.' && temp[start+1] == '.') {
            if (part_count > 0) {
                part_count--;
            }
        } else if (len > 0) {
            if (part_count < 32) {
                parts[part_count] = &temp[start];
                temp[i] = '\0';
                part_count++;
            }
        }
        i++;
    }
    
    if (part_count == 0) {
        output[0] = '/';
        output[1] = '\0';
    } else {
        output[0] = '/';
        int out_idx = 1;
        for (int j = 0; j < part_count; j++) {
            const char* part = parts[j];
            while (*part && out_idx < MAX_PATH_LENGTH - 1) {
                output[out_idx++] = *part++;
            }
            if (j < part_count - 1 && out_idx < MAX_PATH_LENGTH - 1) {
                output[out_idx++] = '/';
            }
        }
        output[out_idx] = '\0';
    }
}

static bool directory_exists(const char* path) {
    vfs_file_t* files = vfs_get_files();
    size_t path_len = strlen(path);
    
    char normalized_path[64];
    size_t i = 0;
    while (path[i] && i < 63) {
        normalized_path[i] = path[i];
        i++;
    }
    normalized_path[i] = '\0';
    
    if (path_len > 1 && normalized_path[path_len - 1] == '/') {
        normalized_path[path_len - 1] = '\0';
        path_len--;
    }
    
    for (size_t i = 0; i < 128; i++) {
        if (!files[i].used || files[i].type != VFS_TYPE_DIR) continue;
        
        size_t name_len = strlen(files[i].name);
        if (name_len < path_len) continue;
        
        bool match = true;
        for (size_t j = 0; j < path_len; j++) {
            if (files[i].name[j] != normalized_path[j]) {
                match = false;
                break;
            }
        }
        
        if (match) {
            if (path_len == 1 && normalized_path[0] == '/') {
                return true;
            }
            
            if (name_len == path_len || 
                (name_len > path_len && files[i].name[path_len] == '/')) {
                return true;
            }
        }
    }
    
    return false;
}

void shell_set_cwd(const char* path) {
    if (path == NULL || path[0] == '\0') {
        strcpy(current_working_directory, "/");
        return;
    }
    
    char normalized_path[MAX_PATH_LENGTH];
    normalize_path(path, normalized_path);
    
    if (!directory_exists(normalized_path)) {
        kprint("cd: ", 7);
        kprint(normalized_path, 7);
        kprint(": No such directory\n", 7);
        return;
    }
    
    strcpy(current_working_directory, normalized_path);
}

void shell_init(void) {
    current_working_directory[0] = '/';
    should_delay_prompt = 0;
    delay_ticks = 0;
    
    kprint("Type 'help' for available commands.\n\n", 7);
}

void shell_run(void) {
    char command[MAX_COMMAND_LENGTH];
    
    while (1) {
        nvm_scheduler_tick();
        
        if (should_delay_prompt) {
            if (delay_ticks > 0) {
                delay_ticks--;
                continue;
            } else {
                should_delay_prompt = 0;
            }
        }
        
        kprint("(host)-[", 7);
        kprint(current_working_directory, 2);
        kprint("] ", 7);
        kprint("# ", 2);
        
        keyboard_getline(command, MAX_COMMAND_LENGTH);
        
        execute_command(command);
    }
}
#include <stddef.h>
#include <core/kernel/mem.h>
#include <core/fs/vfs.h>
#include <core/arch/io.h>
#include <core/kernel/nvm/nvm.h>
#include <core/kernel/nvm/caps.h>
#include <core/kernel/nvm/syscall.h>
#include <core/kernel/kstd.h>
#include <core/fs/procfs.h>

typedef struct {
    uint16_t recipient;
    uint16_t sender;
    uint8_t content;
} message_t;

#define MAX_MESSAGES 32
static message_t message_queue[MAX_MESSAGES];
static int message_count = 0;

int32_t syscall_handler(uint8_t syscall_id, nvm_process_t* proc) {
    int32_t result = 0;
    
    switch(syscall_id) {
        case SYS_EXIT: {
            if(proc->sp >= 1) {
                proc->exit_code = proc->stack[proc->sp - 1];
            } else {
                proc->exit_code = 0;
            }
            proc->active = false;
            procfs_unregister(proc->pid);
            if (proc->bytecode) {
                // kfree(proc->bytecode);
                proc->bytecode = NULL;
            }
            if(proc->sp > 0) proc->sp--;
            break;
        }

        case SYS_SPAWN: {
            if (!caps_has_capability(proc, CAP_FS_READ)) {
                result = -1;
                break;
            }
            if (proc->sp < 1) {
                result = -1;
                break;
            }

            int target_fd = proc->stack[proc->sp - 1];
            int argc = proc->stack[proc->sp - 2];
            if (argc < 0 || argc > 32) { result = -1; break; }
            
            proc->sp -= 2;

            char* argv[argc];
            int arg_index = 0;
            int stack_pos = proc->sp - 1;
            int alloc_error = 0;
            
            while (arg_index < argc && stack_pos >= 0) {
                int end_pos = stack_pos;
                int start_pos = -1;

                while (stack_pos >= 0) {
                    if (proc->stack[stack_pos] == 0) {
                        start_pos = stack_pos + 1;
                        break;
                    }
                    stack_pos--;
                }
                
                if (start_pos == -1 || start_pos > end_pos) {
                    alloc_error = 1;
                    break;
                }

                int len = end_pos - start_pos + 1;
                argv[arg_index] = kmalloc(len + 1);
                if (!argv[arg_index]) {
                    alloc_error = 1;
                    break;
                }

                for (int i = 0; i < len; i++) {
                    argv[arg_index][i] = (char)proc->stack[start_pos + i];
                }
                argv[arg_index][len] = '\0';
                
                arg_index++;
                stack_pos = start_pos - 2;
            }
            
            if (alloc_error) {
                for (int i = 0; i < arg_index; i++) {
                    kfree(argv[i]);
                    argv[i] = NULL;
                }
                result = -1;
                break;
            }

            proc->sp = stack_pos + 1;
            
            uint8_t* bytecode = NULL;
            size_t bytecode_size = 0;
            size_t allocated_size = 1024;

            bytecode = kmalloc(allocated_size);
            if (!bytecode) {
                for (int i = 0; i < argc; i++) {
                    kfree(argv[i]);
                    argv[i] = NULL;
                }
                result = -1;
                break;
            }

            while (1) {
                uint8_t read_byte;
                size_t bytes_read = vfs_readfd(target_fd, &read_byte, 1);
                
                if (bytes_read != 1) {
                    break;
                }

                if (bytecode_size >= allocated_size) {
                    allocated_size *= 2;
                    uint8_t* new_bytecode = kmalloc(allocated_size);
                    if (!new_bytecode) {
                        kfree(bytecode);
                        bytecode = NULL;
                        for (int i = 0; i < argc; i++) {
                            kfree(argv[i]);
                            argv[i] = NULL;
                        }
                        result = -1;
                        break;
                    }

                    for (size_t i = 0; i < bytecode_size; i++) {
                        new_bytecode[i] = bytecode[i];
                    }
                    
                    kfree(bytecode);
                    bytecode = new_bytecode;
                }
                
                bytecode[bytecode_size] = read_byte;
                bytecode_size++;
            }
            
            if (result == -1) {
                if (bytecode) {
                    kfree(bytecode);
                    bytecode = NULL;
                }
                break;
            }

            int total_string_len = 0;
            for (int i = 0; i < argc; i++) {
                total_string_len += strlen(argv[i]) + 1;
            }

            int stack_size = 1 + argc + total_string_len;
            int32_t* initial_stack = kmalloc(stack_size * sizeof(int32_t));
            if (!initial_stack) {
                kfree(bytecode);
                bytecode = NULL;
                for (int i = 0; i < argc; i++) {
                    kfree(argv[i]);
                    argv[i] = NULL;
                }
                result = -1;
                break;
            }

            int stack_pos_init = 0;
            initial_stack[stack_pos_init++] = argc;

            int argv_pointers_start = stack_pos_init;
            stack_pos_init += argc;

            for (int i = 0; i < argc; i++) {
                initial_stack[argv_pointers_start + i] = stack_pos_init;
                
                char* arg = argv[i];
                for (int j = 0; arg[j] != '\0'; j++) {
                    if (stack_pos_init >= stack_size) {
                        kfree(bytecode);
                        bytecode = NULL;
                        kfree(initial_stack);
                        initial_stack = NULL;
                        for (int i = 0; i < argc; i++) {
                            kfree(argv[i]);
                            argv[i] = NULL;
                        }
                        result = -1;
                        break;
                    }
                    initial_stack[stack_pos_init++] = (int32_t)(uint8_t)arg[j];
                }
                if (stack_pos_init >= stack_size) {
                    kfree(bytecode);
                    bytecode = NULL;
                    kfree(initial_stack);
                    initial_stack = NULL;
                    for (int i = 0; i < argc; i++) {
                        kfree(argv[i]);
                        argv[i] = NULL;
                    }
                    result = -1;
                    break;
                }
                initial_stack[stack_pos_init++] = 0;
            }
            
            if (result == -1) {
                break;
            }

            int new_pid = nvm_create_process_with_stack(bytecode, bytecode_size,
                                                    (uint16_t[]){CAPS_NONE}, 1,
                                                    initial_stack, stack_pos_init);
            kfree(initial_stack);
            initial_stack = NULL;
            
            if (new_pid < 0) {
                kfree(bytecode);
                bytecode = NULL;
                for (int i = 0; i < argc; i++) {
                    kfree(argv[i]);
                    argv[i] = NULL;
                }
                result = -1;
                break;
            }

            caps_copy(nvm_get_process(proc->pid), nvm_get_process(new_pid));

            for (int i = 0; i < argc; i++) {
                kfree(argv[i]);
                argv[i] = NULL;
            }

            result = new_pid;
            break;
        }

        case SYS_OPEN: {
            if (!caps_has_capability(proc, CAP_FS_READ)) {
                result = -1;
                break;
            }

            if (proc->sp < 1) {
                result = -1;
                break;
            }

            int start_pos = proc->sp;
            int null_pos = -1;
            
            for (int i = proc->sp - 1; i >= 0; i--) {
                if ((proc->stack[i] & 0xFF) == 0) {
                    null_pos = i;
                    break;
                }
            }
            
            if (null_pos == -1) {
                result = -1;
                break;
            }
            
            char filename[MAX_FILENAME];
            int pos = 0;
       
            for (int i = null_pos + 1; i < start_pos && pos < MAX_FILENAME - 1; i++) {
                char ch = proc->stack[i] & 0xFF;
                filename[pos++] = ch;
            }
            
            filename[pos] = '\0';

            proc->sp = null_pos;
            
            int fd = vfs_open(filename, VFS_READ | VFS_WRITE);
            
            proc->stack[proc->sp] = fd;
            proc->sp++;
            
            result = 0;
            break;
        }

        case SYS_READ: {
            if (!caps_has_capability(proc, CAP_FS_READ)) {
                result = -1;
                break;
            }

            if (proc->sp < 1) {
                result = -1;
                break;
            }
            
            int32_t fd = proc->stack[proc->sp - 1];
            proc->sp--;
            
            if (fd < 0) {
                result = -1;
            } else {
                char buffer;
                size_t bytes = vfs_readfd(fd, &buffer, 1);
                
                if (bytes == 1) {
                    result = (unsigned char)buffer;
                } else if (bytes == 0) {
                    result = 0;
                } else {
                    result = -1;
                }
            }

            proc->stack[proc->sp] = result;
            proc->sp++;
            break;
        }

        case SYS_WRITE: {
            if (!caps_has_capability(proc, CAP_FS_WRITE)) {
                result = -1;
                break;
            }

            if (proc->sp < 2) {
                result = -1;
                break;
            }
            
            int32_t fd = proc->stack[proc->sp - 2];
            int32_t byte_val = proc->stack[proc->sp - 1];
            proc->sp -= 2;
            
            if (fd < 0) {
                result = -1;
            } else if (fd == 1 || fd == 2) {
                char ch = (char)(byte_val & 0xFF);
                char str[2] = {ch, '\0'};
                kprint(str, 15);
                result = 1;
            } else {
                char ch = (char)(byte_val & 0xFF);
                result = vfs_writefd(fd, &ch, 1);
            }
            
            proc->stack[proc->sp] = result;
            proc->sp++;
            break;
        }

        case SYS_MSG_SEND: {
            if (proc->sp < 2) {
                result = -1;
                break;
            }

            uint16_t recipient = proc->stack[proc->sp - 2] & 0xFFFF;
            uint8_t content = proc->stack[proc->sp - 1] & 0xFF;

            if (message_count >= MAX_MESSAGES) {
                result = -1;
                break;
            }

            message_t msg;
            msg.recipient = recipient;
            msg.sender = proc->pid;
            msg.content = content;
            
            message_queue[message_count] = msg;
            message_count++;

            for (int i = 0; i < MAX_PROCESSES; i++) {
                if (processes[i].active && processes[i].pid == recipient && processes[i].blocked) {
                    processes[i].blocked = false; 
                    processes[i].wakeup_reason = 1;
                    break;
                }
            }

            proc->sp -= 2;
            break;
        }

        case SYS_MSG_RECEIVE: {
            int found_index = -1;
            for (int i = 0; i < message_count; i++) {
                if (message_queue[i].recipient == proc->pid) {
                    found_index = i;
                    break;
                }
            }
            
            if (found_index == -1) {
                proc->blocked = true;
                result = -1;
                break;
            }
            
            if (proc->sp + 1 >= STACK_SIZE) {
                result = -1;
                break;
            }
            
            message_t received_msg = message_queue[found_index];
            
            for (int i = found_index; i < message_count - 1; i++) {
                message_queue[i] = message_queue[i + 1];
            }
            message_count--;

            proc->stack[proc->sp] = received_msg.sender;
            proc->stack[proc->sp + 1] = received_msg.content;
            proc->sp += 2;
            break;
        }

        case SYS_PORT_IN_BYTE: {
            if (!caps_has_capability(proc, CAP_DRV_ACCESS)) {
                result = -1;
                break;
            }
            
            if (proc->sp == 0) {
                result = -1;
                break;
            }
            uint16_t port = proc->stack[proc->sp - 1];
            uint8_t val = inb(port);
            
            proc->stack[proc->sp - 1] = (int16_t)val;
            break;
        }

        case SYS_PORT_OUT_BYTE: {
            if (!caps_has_capability(proc, CAP_DRV_ACCESS)) {
                result = -1;
                break;
            }
            if (proc->sp < 2) {
                result = -1;
                break;
            }
            
            uint16_t port = proc->stack[proc->sp - 2] & 0xFFFF;
            uint8_t val = proc->stack[proc->sp - 1] & 0xFF;

            outb(port, val);
            proc->sp -= 2;
            break;
        }

        case SYS_PRINT: {
            if (proc->sp < 1) {
                result = -1;
                break;
            }

            uint8_t val = proc->stack[proc->sp - 1] & 0xFF;
            char print_char[2] = {(char)val, 0};
            kprint(print_char, 15);
            proc->sp -= 1;
            break;
        }

        default: {
            proc->exit_code = -1;
            proc->active = false;
        }
    }
    
    return result;
}
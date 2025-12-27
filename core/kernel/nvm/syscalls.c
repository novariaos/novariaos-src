// SPDX-License-Identifier: LGPL-3.0-or-later

#include <stddef.h>
#include <string.h>
#include <core/kernel/nvm/syscall.h>
#include <core/kernel/nvm/nvm.h>
#include <core/kernel/nvm/caps.h>
#include <core/drivers/serial.h>
#include <core/kernel/log.h>
#include <core/kernel/mem.h>
#include <core/fs/vfs.h>
#include <core/arch/io.h>

uint16_t recipient;
uint16_t port;
uint8_t value;

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
    char buffer[32];
    
    switch(syscall_id) {
        case SYS_EXIT: {
            if(proc->sp >= 1) {
                proc->exit_code = proc->stack[proc->sp - 1];
                itoa(proc->exit_code, buffer, 10);
                LOG_DEBUG("Process %d: exited with code: %s\n", proc->pid, buffer);
            } else {
                proc->exit_code = 0;
            }
            proc->active = false;

            if(proc->sp > 0) proc->sp--;
            break;
        }

        case SYS_SPAWN: {
            if (!caps_has_capability(proc, CAP_FS_READ)) {
                result = -1;
                break;
            }
            if (proc->sp < 1) {
                LOG_WARN("Process %d: Stack underflow for exec\n", proc->pid);
                result = -1;
                break;
            }


            int target_fd = proc->stack[proc->sp - 1];

            int argc = proc->stack[proc->sp - 2];
            
            LOG_TRACE("Process %d: Spawn called with fd=%d, argc=%d\n", 
                    proc->pid, target_fd, argc);

            proc->sp -= 2;

            char* argv[argc];
            int arg_index = 0;

            int stack_pos = proc->sp - 1;
            
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
                    LOG_WARN("Process %d: Malformed string at arg %d\n", 
                            proc->pid, arg_index);
                    result = -1;
                    break;
                }

                int len = end_pos - start_pos + 1;

                argv[arg_index] = kmalloc(len + 1);
                if (!argv[arg_index]) {
                    LOG_WARN("Process %d: Failed to allocate memory\n", proc->pid);
                    result = -1;
                    break;
                }

                for (int i = 0; i < len; i++) {
                    argv[arg_index][i] = (char)proc->stack[start_pos + i];
                }
                argv[arg_index][len] = '\0';
                
                LOG_TRACE("  argv[%d] = '%s'\n", arg_index, argv[arg_index]);
                
                arg_index++;
                stack_pos = start_pos - 2;
            }
            
            if (result == -1) {
                for (int i = 0; i < arg_index; i++) {
                    kfree(argv[i]);
                }
                break;
            }

            proc->sp = stack_pos + 1;
            
            uint8_t* bytecode = NULL;
            size_t bytecode_size = 0;
            size_t allocated_size = 1024;

            bytecode = kmalloc(allocated_size);
            if (!bytecode) {
                LOG_WARN("Process %d: Failed to allocate memory for bytecode\n", proc->pid);
                for (int i = 0; i < argc; i++) {
                    kfree(argv[i]);
                }
                result = -1;
                break;
            }

            while (1) {
                uint8_t read_byte;
                size_t bytes_read = vfs_readfd(target_fd, &read_byte, 1);
                
                if (bytes_read != 1) {
                    if (bytes_read == 0) {
                        LOG_DEBUG("Process %d: End of file reached, read %d bytes\n", 
                                proc->pid, bytecode_size);
                    } else {
                        LOG_WARN("Process %d: Error reading file\n", proc->pid);
                    }
                    break;
                }

                if (bytecode_size >= allocated_size) {
                    allocated_size *= 2;
                    uint8_t* new_bytecode = kmalloc(allocated_size);
                    if (!new_bytecode) {
                        LOG_WARN("Process %d: Failed to reallocate bytecode buffer\n", proc->pid);
                        kfree(bytecode);
                        for (int i = 0; i < argc; i++) {
                            kfree(argv[i]);
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
                break;
            }

            int initial_stack_size = 1;

            for (int i = 0; i < argc; i++) {
                initial_stack_size += strlen(argv[i]) + 1;
            }

            int32_t* initial_stack = kmalloc(initial_stack_size * sizeof(int32_t));
            if (!initial_stack) {
                LOG_WARN("Process %d: Failed to allocate initial stack\n", proc->pid);
                kfree(bytecode);
                for (int i = 0; i < argc; i++) {
                    kfree(argv[i]);
                }
                result = -1;
                break;
            }

            stack_pos = 0;

            for (int i = argc - 1; i >= 0; i--) {
                char* arg = argv[i];
                for (int j = 0; arg[j] != '\0'; j++) {
                    initial_stack[stack_pos++] = (int32_t)(uint8_t)arg[j];
                }
                initial_stack[stack_pos++] = 0;
            }

            initial_stack[stack_pos++] = argc;

            int new_pid = nvm_create_process_with_stack(bytecode, bytecode_size,
                                                      (uint16_t[]){CAPS_NONE}, 1,
                                                      initial_stack, stack_pos);
            kfree(initial_stack);
            caps_copy(nvm_get_process(proc->pid), nvm_get_process(new_pid));

            if (new_pid < 0) {
                LOG_WARN("Process %d: Failed to create new process\n", proc->pid);
                kfree(bytecode);
                for (int i = 0; i < argc; i++) {
                    kfree(argv[i]);
                }
                result = -1;
                break;
            }

            LOG_INFO("Process %d: Spawn process with pid %d\n", proc->pid, new_pid);

            kfree(bytecode);
            for (int i = 0; i < argc; i++) {
                kfree(argv[i]);
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
            
            LOG_TRACE("Process %d: Opening file '%s'\n", proc->pid, filename);
            
            int fd = vfs_open(filename, VFS_READ | VFS_WRITE);
            
            LOG_TRACE("     vfs_open returned: %d\n", fd);
            
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
                    result = 0;  // EOF
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
                    LOG_WARN("Process %d: Stack underflow for msg_send\n", proc->pid);
                    result = -1;
                    break;
                }

                recipient = proc->stack[proc->sp - 2] & 0xFFFF;
                value = proc->stack[proc->sp - 1] & 0xFF;

                if (message_count >= MAX_MESSAGES) {
                    LOG_WARN("Process %d: Message queue full\n", proc->pid);
                    result = -1;
                    break;
                }

                message_t msg;
                msg.recipient = recipient;
                msg.sender = proc->pid;
                msg.content = value;
                
                message_queue[message_count] = msg;
                message_count++;

                for (int i = 0; i < MAX_PROCESSES; i++) {
                    if (processes[i].active && processes[i].pid == recipient && processes[i].blocked) {
                        processes[i].blocked = false; 
                        processes[i].wakeup_reason = 1;
                        LOG_DEBUG("Unblocked process %d due to incoming message\n", recipient);
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
                serial_print("No messages for process ");
                itoa(proc->pid, buffer, 10);
                serial_print(buffer);
                serial_print(" - blocking process\n");
                LOG_DEBUG("Process %d: No messages - blocking\n", proc->pid);
                proc->blocked = true;
                result = -1;
                break;
            }
            
            message_t received_msg = message_queue[found_index];
            
            for (int i = found_index; i < message_count - 1; i++) {
                message_queue[i] = message_queue[i + 1];
            }
            message_count--;

            if (proc->sp + 1 < 256) {
                proc->stack[proc->sp] = received_msg.sender;
                proc->stack[proc->sp + 1] = received_msg.content;
                proc->sp += 2;
            } else {
                LOG_DEBUG("Process %d: Stack overflow in msg_receive\n", proc->pid);
                result = -1;
                break;
            }

            itoa(proc->pid, buffer, 10);
            LOG_DEBUG("Process %d: Message received\n", proc->pid);
            break;
        }

        case SYS_PORT_IN_BYTE: {
            if (!caps_has_capability(proc, CAP_DRV_ACCESS)) {
                LOG_WARN("Process %d: Terminate process - required caps not received\n", proc->pid);
                result = -1;
                break;
            }
            
            if (proc->sp == 0) {
                LOG_WARN("Process %d: Stack empty for inb\n", proc->pid);
                result = -1;
                break;
            }
            port = proc->stack[proc->sp - 1];
            value = inb(port);
            
            proc->stack[proc->sp - 1] = (int16_t)value;
            break;
        }

        case SYS_PORT_OUT_BYTE: {
            if (proc->sp < 2) {
                LOG_WARN("Process %d: Stack underflow for outb\n", proc->pid);
                result = -1;
                break;
            }
            
            port = proc->stack[proc->sp - 2] & 0xFFFF;
            value = proc->stack[proc->sp - 1] & 0xFF;

            outb(port, value);
            proc->sp -= 2;
            break;
        }

        case SYS_PRINT: {
            if (proc->sp < 1) {
                LOG_WARN("Process %d: Stack underflow for print\n", proc->pid);
                result = -1;
                break;
            }

            value = proc->stack[proc->sp - 1] & 0xFF;
            char print_char[2] = {(char)value, 0};
            kprint(print_char, 15);
            proc->sp -= 1;
            break;
        }

        default: {
            itoa(syscall_id, buffer, 16);
            LOG_WARN("Process %d: unknown syscall: 0x%s\n", proc->pid, buffer);
            proc->exit_code = -1;
            proc->active = false;
        }
    }
    
    return result;
}

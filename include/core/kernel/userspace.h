#ifndef _USERSPACE_H_
#define _USERSPACE_H_

#include <stdint.h>
#include <stdbool.h>

// Userspace program entry point
typedef int (*userspace_program_t)(int argc, char** argv);

// Register a userspace program
void userspace_register(const char* name, userspace_program_t program);

// Execute a userspace program
int userspace_exec(const char* name, int argc, char** argv);

// Check if program exists
bool userspace_exists(const char* name);

// List all registered programs
void userspace_list(void);

#endif // _USERSPACE_H_

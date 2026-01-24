#ifndef MEM_H
#define MEM_H

#include <core/kernel/mem/allocator.h>

// Legacy aliases for backward compatibility
#define initializeMemoryManager memory_manager_init
#define allocateMemory kmalloc
#define freeMemory kfree
#define mm_test memory_test
#define getMemTotal get_memory_total
#define getMemFree get_memory_free
#define getMemUsed get_memory_used
#define getMemAvailable get_memory_available
#define formatMemorySize format_memory_size

#endif

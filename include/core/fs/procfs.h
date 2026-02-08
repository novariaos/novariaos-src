#ifndef PROCFS_H
#define PROCFS_H

#include <core/fs/vfs.h>

void procfs_register(int pid, void* process_data);
void procfs_unregister(int pid);
vfs_ssize_t procfs_cpu(vfs_file_t* file, void* buf, size_t count, vfs_off_t* pos);
vfs_ssize_t procfs_cpuinfo(vfs_file_t* file, void* buf, size_t count, vfs_off_t* pos);
vfs_ssize_t procfs_meminfo(vfs_file_t* file, void* buf, size_t count, vfs_off_t* pos);
vfs_ssize_t procfs_pci(vfs_file_t* file, void* buf, size_t count, vfs_off_t* pos);
vfs_ssize_t procfs_uptime(vfs_file_t* file, void* buf, size_t count, vfs_off_t* pos);
vfs_ssize_t procfs_version(vfs_file_t* file, void* buf, size_t count, vfs_off_t* pos);
void procfs_init(void);
void cpuinfo_init(void);

#endif
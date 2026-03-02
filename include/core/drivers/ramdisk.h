#ifndef RAMDISK_H
#define RAMDISK_H

#include <stddef.h>

// Register a memory-mapped disk image as a read-only block device.
// name:  block device name (e.g. "hda")
// image: pointer to the image in memory
// size:  image size in bytes (must be a multiple of 512)
void ramdisk_register(const char* name, void* image, size_t size);

#endif // RAMDISK_H

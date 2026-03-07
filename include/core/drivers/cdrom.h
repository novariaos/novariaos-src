#ifndef CDROM_H
#define CDROM_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// Initialize CD-ROM driver and detect the boot ISO
bool cdrom_init(void);

// Set ISO data pointer (used during boot to point to the ISO in memory)
void cdrom_set_iso_data(void* data, size_t size);

#endif // CDROM_H


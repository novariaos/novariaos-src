// SPDX-License-Identifier: GPL-3.0-only

#ifndef IDE_H
#define IDE_H

// Detect and register all IDE drives (primary/secondary × master/slave).
// Each found drive is registered as a block device: hda, hdb, hdc, hdd.
// Uses ATA PIO polling — no IRQ required.
void ide_init(void);

#endif

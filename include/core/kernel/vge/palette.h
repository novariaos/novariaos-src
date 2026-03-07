// SPDX-License-Identifier: GPL-3.0-only

#ifndef _PALETTE_H_
#define _PALETTE_H_

#include <stdint.h>

#define PALETTE_PATH "/etc/palette.conf"

void palette_init(void);
uint32_t palette_get_color(int index);

#endif // _PALETTE_H_

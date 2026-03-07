#ifndef _PSF_H
#define _PSF_H

#include <stdint.h>

#define PSF1_MAGIC 0x0436
#define PSF2_MAGIC 0x864ab572
#define PSF2_HAS_UNICODE_TABLE 0x01

struct psf2_header {
    uint32_t magic;       // 0x864ab572
    uint32_t version;     // 0
    uint32_t headersize;  // 32
    uint32_t flags;       // 0 = without Unicode table ; 1 = with
    uint32_t length;      // number of glyphs
    uint32_t charsize;    // glyph size in byte
    uint32_t height;      // height in pixels
    uint32_t width;       // width in pixels (regular 8)
};

#endif
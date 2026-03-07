#include <core/fs/vfs.h>
#include <core/kernel/mem.h>
#include <core/kernel/vge/fb_render.h>
#include <core/kernel/kstd.h>
#include <stdint.h>
#include <stddef.h>

#pragma pack(push,1)
typedef struct {
    uint16_t magic;
    uint8_t  mode;
    uint8_t  charsize;
} psf1_header_t;

#define PSF1_MAGIC 0x0436
#define PSF1_MODE512 0x01
#define PSF1_MODEHASTAB 0x02
#define FONT_HEIGHT 16

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t headersize;
    uint32_t flags;
    uint32_t length;
    uint32_t charsize;
    uint32_t height;
    uint32_t width;
} psf2_header_t;

#define PSF2_MAGIC 0x864ab572
#define PSF2_HAS_UNICODE_TABLE 0x01
#pragma pack(pop)

int parse_psf_to_font(const uint8_t* psf_data, size_t psf_size, uint8_t font[256][FONT_HEIGHT]) {
    if (!psf_data || psf_size < 4) return -1;

    if (psf_size >= sizeof(psf2_header_t)) {
        const psf2_header_t *h2 = (const psf2_header_t *)psf_data;
        if (h2->magic == PSF2_MAGIC && h2->headersize >= sizeof(psf2_header_t)) {
            if ((uintptr_t)h2 + sizeof(psf2_header_t) > (uintptr_t)psf_data + psf_size) return -1;
            
            if (h2->width != 8 || h2->height < 8 || h2->length < 256) return -1;

            size_t expected_size = (size_t)h2->headersize + (size_t)h2->length * (size_t)h2->charsize;
            if (expected_size > psf_size || expected_size < (size_t)h2->headersize) return -1;

            const uint8_t* glyphs = psf_data + h2->headersize;
            
            if ((uintptr_t)glyphs + h2->length * h2->charsize > (uintptr_t)psf_data + psf_size) return -1;

            int copy_height = (h2->height > FONT_HEIGHT) ? FONT_HEIGHT : h2->height;
            int bytes_per_row = (h2->width + 7) / 8; 

            for (int i = 0; i < 256; i++) {
                for (int row = 0; row < FONT_HEIGHT; row++) {
                    font[i][row] = 0;
                }
            }

            for (int i = 0; i < 256 && i < h2->length; i++) {
                const uint8_t* glyph_start = glyphs + i * h2->charsize;
                
                for (int row = 0; row < copy_height; row++) {
                    if (row * bytes_per_row < h2->charsize) {
                        font[i][row] = glyph_start[row * bytes_per_row];
                    }
                }
            }

            kprint("PSF2 font loaded: ", 7);
            kprint("height=", 7);
            
            char height_str[16];
            for (int i = 0; i < 16; i++) height_str[i] = 0;
            int idx = 0;
            int temp = h2->height;
            do {
                height_str[idx++] = '0' + (temp % 10);
                temp /= 10;
            } while (temp > 0 && idx < 15);
            
            for (int i = 0; i < idx/2; i++) {
                char t = height_str[i];
                height_str[i] = height_str[idx-1-i];
                height_str[idx-1-i] = t;
            }
            
            kprint(height_str, 7);
            kprint(" chars=", 7);
            
            char length_str[16];
            for (int i = 0; i < 16; i++) length_str[i] = 0;
            idx = 0;
            temp = h2->length;
            do {
                length_str[idx++] = '0' + (temp % 10);
                temp /= 10;
            } while (temp > 0 && idx < 15);
            
            for (int i = 0; i < idx/2; i++) {
                char t = length_str[i];
                length_str[i] = length_str[idx-1-i];
                length_str[idx-1-i] = t;
            }
            
            kprint(length_str, 7);
            kprint("\n", 7);

            return 0;
        }
    }

    if (psf_size >= sizeof(psf1_header_t)) {
        const psf1_header_t *h1 = (const psf1_header_t *)psf_data;
        if (h1->magic == PSF1_MAGIC) {
            if ((uintptr_t)h1 + sizeof(psf1_header_t) > (uintptr_t)psf_data + psf_size) return -1;
            
            uint32_t glyphs = (h1->mode & PSF1_MODE512) ? 512 : 256;
            size_t bitmapsz = (size_t)glyphs * (size_t)h1->charsize;
            size_t header_size = sizeof(psf1_header_t);

            if (header_size + bitmapsz > psf_size) return -1;
            if (h1->charsize < 8 || glyphs < 256) return -1;

            const uint8_t* bitmap = psf_data + header_size;

            if ((uintptr_t)bitmap + bitmapsz > (uintptr_t)psf_data + psf_size) return -1;

            for (int i = 0; i < 256; i++) {
                for (int row = 0; row < FONT_HEIGHT; row++) {
                    font[i][row] = 0;
                }
            }

            int copy_height = (h1->charsize > FONT_HEIGHT) ? FONT_HEIGHT : h1->charsize;
            for (int i = 0; i < 256 && i < glyphs; i++) {
                for (int row = 0; row < copy_height; row++) {
                    font[i][row] = bitmap[i * h1->charsize + row];
                }
            }
            
            kprint("PSF1 font loaded\n", 7);
            return 0;
        }
    }

    kprint("Unknown font format\n", 12);
    return -1;
}

int load_font_from_vfs(const char* path, uint8_t font[256][FONT_HEIGHT]) {
    kprint("Loading font: ", 7);
    kprint(path, 7);
    kprint("\n", 7);
    
    size_t file_size = 0;
    const char* psf_data = vfs_read(path, &file_size);

    if (!psf_data) {
        kprint("  Failed to read file\n", 12);
        return -1;
    }
    
    kprint("  File size: ", 7);
    char size_str[32];
    for (int i = 0; i < 32; i++) size_str[i] = 0;
    int idx = 0;
    size_t temp = file_size;
    do {
        size_str[idx++] = '0' + (temp % 10);
        temp /= 10;
    } while (temp > 0 && idx < 31);
    
    for (int i = 0; i < idx/2; i++) {
        char t = size_str[i];
        size_str[i] = size_str[idx-1-i];
        size_str[idx-1-i] = t;
    }
    
    kprint(size_str, 7);
    kprint(" bytes\n", 7);

    int result = parse_psf_to_font((const uint8_t*)psf_data, file_size, font);
    
    if (result == 0) {
        kprint("  Font parsed successfully\n", 2);
    } else {
        kprint("  Font parsing failed: ", 12);
        char err_str[16];
        err_str[0] = '0' + (-result % 10);
        err_str[1] = '\0';
        kprint(err_str, 12);
        kprint("\n", 12);
    }
    
    return result;
}
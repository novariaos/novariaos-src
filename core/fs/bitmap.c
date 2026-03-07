// SPDX-License-Identifier: GPL-3.0-only

#include <core/fs/bitmap.h>

void bitmap_set(uint8_t* bitmap, size_t bit) {
    size_t byte_index = bit / 8;
    size_t bit_offset = bit % 8;
    bitmap[byte_index] |= (1 << bit_offset);
}

void bitmap_clear(uint8_t* bitmap, size_t bit) {
    size_t byte_index = bit / 8;
    size_t bit_offset = bit % 8;
    bitmap[byte_index] &= ~(1 << bit_offset);
}

bool bitmap_test(const uint8_t* bitmap, size_t bit) {
    size_t byte_index = bit / 8;
    size_t bit_offset = bit % 8;
    return (bitmap[byte_index] & (1 << bit_offset)) != 0;
}

int bitmap_find_first_free(const uint8_t* bitmap, size_t num_bits) {
    size_t num_bytes = (num_bits + 7) / 8;

    for (size_t i = 0; i < num_bytes; i++) {
        if (bitmap[i] != 0xFF) {
            for (size_t j = 0; j < 8; j++) {
                size_t bit = i * 8 + j;
                if (bit >= num_bits) {
                    return -1;
                }
                if (!bitmap_test(bitmap, bit)) {
                    return (int)bit;
                }
            }
        }
    }

    return -1;
}

int bitmap_find_first_set(const uint8_t* bitmap, size_t num_bits) {
    size_t num_bytes = (num_bits + 7) / 8;

    for (size_t i = 0; i < num_bytes; i++) {
        if (bitmap[i] != 0x00) {
            for (size_t j = 0; j < 8; j++) {
                size_t bit = i * 8 + j;
                if (bit >= num_bits) {
                    return -1;
                }
                if (bitmap_test(bitmap, bit)) {
                    return (int)bit;
                }
            }
        }
    }

    return -1;
}

size_t bitmap_count_free(const uint8_t* bitmap, size_t num_bits) {
    size_t count = 0;
    size_t num_bytes = (num_bits + 7) / 8;

    for (size_t i = 0; i < num_bytes; i++) {
        uint8_t byte = bitmap[i];
        for (size_t j = 0; j < 8; j++) {
            size_t bit = i * 8 + j;
            if (bit >= num_bits) {
                break;
            }
            if ((byte & (1 << j)) == 0) {
                count++;
            }
        }
    }

    return count;
}

size_t bitmap_count_set(const uint8_t* bitmap, size_t num_bits) {
    size_t count = 0;
    size_t num_bytes = (num_bits + 7) / 8;

    for (size_t i = 0; i < num_bytes; i++) {
        uint8_t byte = bitmap[i];
        for (size_t j = 0; j < 8; j++) {
            size_t bit = i * 8 + j;
            if (bit >= num_bits) {
                break;
            }
            if ((byte & (1 << j)) != 0) {
                count++;
            }
        }
    }

    return count;
}

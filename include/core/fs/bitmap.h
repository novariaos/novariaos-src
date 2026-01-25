#ifndef BITMAP_H
#define BITMAP_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// Bitmap operations for filesystem block/inode allocation

// Set bit at position 'bit' in bitmap
void bitmap_set(uint8_t* bitmap, size_t bit);

// Clear bit at position 'bit' in bitmap
void bitmap_clear(uint8_t* bitmap, size_t bit);

// Test if bit at position 'bit' is set
bool bitmap_test(const uint8_t* bitmap, size_t bit);

// Find first free (zero) bit in bitmap, returns bit position or -1 if none found
int bitmap_find_first_free(const uint8_t* bitmap, size_t num_bits);

// Find first set (one) bit in bitmap, returns bit position or -1 if none found
int bitmap_find_first_set(const uint8_t* bitmap, size_t num_bits);

// Count number of free (zero) bits in bitmap
size_t bitmap_count_free(const uint8_t* bitmap, size_t num_bits);

// Count number of set (one) bits in bitmap
size_t bitmap_count_set(const uint8_t* bitmap, size_t num_bits);

#endif // BITMAP_H

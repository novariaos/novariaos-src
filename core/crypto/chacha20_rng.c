// SPDX-License-Identifier: GPL-3.0-only

#include <core/crypto/chacha20_rng.h>
#include <stdint.h>

static int32_t rotl32(int32_t x, int n) {
    return (x << n) | (x >> (32 - n));
}

static int32_t pack4(const int8_t *a) {
    return ((int32_t)a[0]) | ((int32_t)a[1] << 8) | 
           ((int32_t)a[2] << 16) | ((int32_t)a[3] << 24);
}

static void memory_copy(int8_t *dst, const int8_t *src, int32_t n) {
    for(int32_t i = 0; i < n; i++) dst[i] = src[i];
}

static void memory_set(int8_t *dst, int8_t val, int32_t n) {
    for(int32_t i = 0; i < n; i++) dst[i] = val;
}

static void chacha20_init_block(struct chacha20_rng *rng, int8_t key[], int8_t nonce[]) {
    const int8_t magic[16] = {101,120,112,97,110,100,32,51,50,45,98,121,116,101,32,107};
    
    memory_copy(rng->key, key, 32);
    memory_copy(rng->nonce, nonce, 12);
    
    rng->state[0] = pack4(magic);
    rng->state[1] = pack4(magic + 4);
    rng->state[2] = pack4(magic + 8);
    rng->state[3] = pack4(magic + 12);
    
    for(int i = 0; i < 8; i++) rng->state[4 + i] = pack4(key + i * 4);
    
    rng->state[12] = 0;
    rng->state[13] = pack4(nonce);
    rng->state[14] = pack4(nonce + 4);
    rng->state[15] = pack4(nonce + 8);
}

static void chacha20_block_set_counter(struct chacha20_rng *rng, uint64_t counter) {
    rng->state[12] = (int32_t)counter;                 // Младшие 32 бита
    rng->state[13] = pack4(rng->nonce) + (int32_t)(counter >> 32); // Старшие 32 бита
}

static void chacha20_block_next(struct chacha20_rng *rng) {
    int32_t x[16];
    for(int i = 0; i < 16; i++) x[i] = rng->state[i];
    
    for(int i = 0; i < 10; i++) {
        x[0] += x[4]; x[12] = rotl32(x[12] ^ x[0], 16);
        x[8] += x[12]; x[4] = rotl32(x[4] ^ x[8], 12);
        x[0] += x[4]; x[12] = rotl32(x[12] ^ x[0], 8);
        x[8] += x[12]; x[4] = rotl32(x[4] ^ x[8], 7);
        
        x[1] += x[5]; x[13] = rotl32(x[13] ^ x[1], 16);
        x[9] += x[13]; x[5] = rotl32(x[5] ^ x[9], 12);
        x[1] += x[5]; x[13] = rotl32(x[13] ^ x[1], 8);
        x[9] += x[13]; x[5] = rotl32(x[5] ^ x[9], 7);
        
        x[2] += x[6]; x[14] = rotl32(x[14] ^ x[2], 16);
        x[10] += x[14]; x[6] = rotl32(x[6] ^ x[10], 12);
        x[2] += x[6]; x[14] = rotl32(x[14] ^ x[2], 8);
        x[10] += x[14]; x[6] = rotl32(x[6] ^ x[10], 7);
        
        x[3] += x[7]; x[15] = rotl32(x[15] ^ x[3], 16);
        x[11] += x[15]; x[7] = rotl32(x[7] ^ x[11], 12);
        x[3] += x[7]; x[15] = rotl32(x[15] ^ x[3], 8);
        x[11] += x[15]; x[7] = rotl32(x[7] ^ x[11], 7);
        
        x[0] += x[5]; x[15] = rotl32(x[15] ^ x[0], 16);
        x[10] += x[15]; x[5] = rotl32(x[5] ^ x[10], 12);
        x[0] += x[5]; x[15] = rotl32(x[15] ^ x[0], 8);
        x[10] += x[15]; x[5] = rotl32(x[5] ^ x[10], 7);
        
        x[1] += x[6]; x[12] = rotl32(x[12] ^ x[1], 16);
        x[11] += x[12]; x[6] = rotl32(x[6] ^ x[11], 12);
        x[1] += x[6]; x[12] = rotl32(x[12] ^ x[1], 8);
        x[11] += x[12]; x[6] = rotl32(x[6] ^ x[11], 7);
        
        x[2] += x[7]; x[13] = rotl32(x[13] ^ x[2], 16);
        x[8] += x[13]; x[7] = rotl32(x[7] ^ x[8], 12);
        x[2] += x[7]; x[13] = rotl32(x[13] ^ x[2], 8);
        x[8] += x[13]; x[7] = rotl32(x[7] ^ x[8], 7);
        
        x[3] += x[4]; x[14] = rotl32(x[14] ^ x[3], 16);
        x[9] += x[14]; x[4] = rotl32(x[4] ^ x[9], 12);
        x[3] += x[4]; x[14] = rotl32(x[14] ^ x[3], 8);
        x[9] += x[14]; x[4] = rotl32(x[4] ^ x[9], 7);
    }
    
    for(int i = 0; i < 16; i++) rng->keystream32[i] = x[i] + rng->state[i];
    
    // Инкремент 64-битного счетчика, хранящегося в state[12] и state[13]
    uint64_t counter = ((uint64_t)rng->state[13] << 32) | (uint32_t)rng->state[12];
    counter++;
    rng->state[12] = (int32_t)counter;               // Младшие 32 бита
    rng->state[13] = (int32_t)(counter >> 32);       // Старшие 32 бита
}

void chacha20_rng_init(struct chacha20_rng *rng, uint64_t seed) {
    memory_set((int8_t*)rng, 0, sizeof(struct chacha20_rng));
    
    int8_t key[32];
    int8_t nonce[12];
    
    for(int i = 0; i < 32; i++) key[i] = (int8_t)(seed >> (i % 8 * 8));
    for(int i = 0; i < 12; i++) nonce[i] = (int8_t)(seed >> (i % 8 * 8)) ^ 0xAA;
    
    chacha20_init_block(rng, key, nonce);
    chacha20_block_set_counter(rng, 0);
    rng->counter = 0;
    rng->position = 64;
}

int32_t chacha20_rng_next32(struct chacha20_rng *rng) {
    if(rng->position >= 64) {
        chacha20_block_next(rng);
        rng->position = 0;
    }
    
    int32_t *keystream32 = rng->keystream32;
    int32_t result = keystream32[rng->position / 4];
    rng->position += 4;
    
    return result;
}

uint64_t chacha20_rng_next64(struct chacha20_rng *rng) {
    uint32_t high = (uint32_t)chacha20_rng_next32(rng);
    uint32_t low = (uint32_t)chacha20_rng_next32(rng);
    return ((uint64_t)high << 32) | low;
}

void chacha20_rng_bytes(struct chacha20_rng *rng, int8_t *buffer, int32_t size) {
    int8_t *keystream8 = (int8_t*)rng->keystream32;
    
    for(int32_t i = 0; i < size; i++) {
        if(rng->position >= 64) {
            chacha20_block_next(rng);
            rng->position = 0;
        }
        buffer[i] = keystream8[rng->position];
        rng->position++;
    }
}
// SPDX-License-Identifier: GPL-3.0-only

#include <core/crypto/chacha20.h>

static int32_t rotl32(int32_t x, int n) {
    return (x << n) | (x >> (32 - n));
}

static int32_t pack4(const int8_t *a) {
    int32_t res = 0;
    res |= (int32_t)a[0] << 0;
    res |= (int32_t)a[1] << 8;
    res |= (int32_t)a[2] << 16;
    res |= (int32_t)a[3] << 24;
    return res;
}

static void unpack4(int32_t src, int8_t *dst) {
    dst[0] = (int8_t)(src >> 0);
    dst[1] = (int8_t)(src >> 8);
    dst[2] = (int8_t)(src >> 16);
    dst[3] = (int8_t)(src >> 24);
}

static void memory_copy(int8_t *dst, const int8_t *src, int32_t n) {
    for(int32_t i = 0; i < n; i++) {
        dst[i] = src[i];
    }
}

static void memory_set(int8_t *dst, int8_t val, int32_t n) {
    for(int32_t i = 0; i < n; i++) {
        dst[i] = val;
    }
}

static void chacha20_init_block(struct chacha20_context *ctx, int8_t key[], int8_t nonce[]) {
    const int8_t magic_constant[16] = {
        'e', 'x', 'p', 'a', 'n', 'd', ' ', '3',
        '2', '-', 'b', 'y', 't', 'e', ' ', 'k'
    };
    
    memory_copy(ctx->key, key, 32);
    memory_copy(ctx->nonce, nonce, 12);
    
    ctx->state[0] = pack4(magic_constant + 0);
    ctx->state[1] = pack4(magic_constant + 4);
    ctx->state[2] = pack4(magic_constant + 8);
    ctx->state[3] = pack4(magic_constant + 12);
    
    ctx->state[4] = pack4(key + 0);
    ctx->state[5] = pack4(key + 4);
    ctx->state[6] = pack4(key + 8);
    ctx->state[7] = pack4(key + 12);
    ctx->state[8] = pack4(key + 16);
    ctx->state[9] = pack4(key + 20);
    ctx->state[10] = pack4(key + 24);
    ctx->state[11] = pack4(key + 28);
    
    ctx->state[12] = 0;
    ctx->state[13] = pack4(nonce + 0);
    ctx->state[14] = pack4(nonce + 4);
    ctx->state[15] = pack4(nonce + 8);
}

static void chacha20_block_set_counter(struct chacha20_context *ctx, int counter) {
    ctx->state[12] = counter;
    ctx->state[13] = pack4(ctx->nonce + 0);
}

static void chacha20_block_next(struct chacha20_context *ctx) {
    int32_t x[16];
    
    for(int i = 0; i < 16; i++) {
        x[i] = ctx->state[i];
    }
    
    for(int i = 0; i < 10; i++)
    {
        // Column rounds
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
        
        // Diagonal rounds
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
    
    for(int i = 0; i < 16; i++) {
        ctx->keystream32[i] = x[i] + ctx->state[i];
    }
    
    int32_t *counter = &ctx->state[12];
    counter[0]++;
    if(counter[0] == 0) {
        counter[1]++;
    }
}

void chacha20_init_context(struct chacha20_context *ctx, int8_t key[], int8_t nonce[], int counter) {
    memory_set((int8_t*)ctx, 0, sizeof(struct chacha20_context));
    chacha20_init_block(ctx, key, nonce);
    chacha20_block_set_counter(ctx, counter);
    ctx->counter = counter;
    ctx->position = 64;
}

void chacha20_xor(struct chacha20_context *ctx, int8_t *bytes, int32_t n_bytes) {
    int8_t *keystream8 = (int8_t*)ctx->keystream32;
    
    for(int32_t i = 0; i < n_bytes; i++)
    {
        if(ctx->position >= 64)
        {
            chacha20_block_next(ctx);
            ctx->position = 0;
        }
        bytes[i] ^= keystream8[ctx->position];
        ctx->position++;
    }
}
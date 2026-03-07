#ifndef CHACHA20_H
#define CHACHA20_H

#include <stdint.h>

#define NULL ((void*)0)

struct chacha20_context
{
    int32_t keystream32[16];
    int32_t position;
    
    int8_t key[32];
    int8_t nonce[12];
    int counter;
    
    int32_t state[16];
};

void chacha20_init_context(struct chacha20_context *ctx, int8_t key[], int8_t nonce[], int counter);
void chacha20_xor(struct chacha20_context *ctx, int8_t *bytes, int32_t n_bytes);

#endif
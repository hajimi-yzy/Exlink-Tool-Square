#pragma once
#include <stdint.h>
#include <stddef.h>
// Deterministic transaction-test digest only. NOT SHA-256, NOT a crypto test.
struct mbedtls_sha256_context { uint32_t state; uint32_t length; };
inline void mbedtls_sha256_init(mbedtls_sha256_context *ctx) { ctx->state=2166136261U; ctx->length=0; }
inline void mbedtls_sha256_free(mbedtls_sha256_context *) {}
inline int mbedtls_sha256_starts_ret(mbedtls_sha256_context *ctx,int) { mbedtls_sha256_init(ctx); return 0; }
inline int mbedtls_sha256_update_ret(mbedtls_sha256_context *ctx,const unsigned char *data,size_t size) {
    for(size_t i=0;i<size;++i) { ctx->state=(ctx->state^data[i])*16777619U; ++ctx->length; }
    return 0;
}
inline int mbedtls_sha256_finish_ret(mbedtls_sha256_context *ctx,unsigned char output[32]) {
    for(unsigned i=0;i<32;++i) output[i]=static_cast<unsigned char>((ctx->state>>(8*(i%4)))^(ctx->length+i));
    return 0;
}

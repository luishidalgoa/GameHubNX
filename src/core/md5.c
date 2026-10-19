#include "md5.h"

#include <string.h>

#define ROTL32(x, n) (((x) << (n)) | ((x) >> (32 - (n))))

static const uint32_t K[64] = {
    0xD76AA478u,     0xE8C7B756u,     0x242070DBu,     0xC1BDCEEEu,
    0xF57C0FAFu,     0x4787C62Au,     0xA8304613u,     0xFD469501u,
    0x698098D8u,     0x8B44F7AFu,     0xFFFF5BB1u,     0x895CD7BEu,
    0x6B901122u,     0xFD987193u,     0xA679438Eu,     0x49B40821u,
    0xF61E2562u,     0xC040B340u,     0x265E5A51u,     0xE9B6C7AAu,
    0xD62F105Du,     0x02441453u,     0xD8A1E681u,     0xE7D3FBC8u,
    0x21E1CDE6u,     0xC33707D6u,     0xF4D50D87u,     0x455A14EDu,
    0xA9E3E905u,     0xFCEFA3F8u,     0x676F02D9u,     0x8D2A4C8Au,
    0xFFFA3942u,     0x8771F681u,     0x6D9D6122u,     0xFDE5380Cu,
    0xA4BEEA44u,     0x4BDECFA9u,     0xF6BB4B60u,     0xBEBFBC70u,
    0x289B7EC6u,     0xEAA127FAu,     0xD4EF3085u,     0x04881D05u,
    0xD9D4D039u,     0xE6DB99E5u,     0x1FA27CF8u,     0xC4AC5665u,
    0xF4292244u,     0x432AFF97u,     0xAB9423A7u,     0xFC93A039u,
    0x655B59C3u,     0x8F0CCC92u,     0xFFEFF47Du,     0x85845DD1u,
    0x6FA87E4Fu,     0xFE2CE6E0u,     0xA3014314u,     0x4E0811A1u,
    0xF7537E82u,     0xBD3AF235u,     0x2AD7D2BBu,     0xEB86D391u,
};

static const uint8_t S[64] = {
    7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
    5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
    4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
    6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21,
};

static void md5_transform(uint32_t state[4], const uint8_t block[64]) {
    uint32_t M[16];
    for (int i = 0; i < 16; ++i)
        M[i] = (uint32_t)block[i * 4] | ((uint32_t)block[i * 4 + 1] << 8) |
               ((uint32_t)block[i * 4 + 2] << 16) |
               ((uint32_t)block[i * 4 + 3] << 24);

    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    for (int i = 0; i < 64; ++i) {
        uint32_t f;
        unsigned g;
        if (i < 16) {
            f = (b & c) | (~b & d);
            g = (unsigned)i;
        } else if (i < 32) {
            f = (d & b) | (~d & c);
            g = (unsigned)((5 * i + 1) & 15);
        } else if (i < 48) {
            f = b ^ c ^ d;
            g = (unsigned)((3 * i + 5) & 15);
        } else {
            f = c ^ (b | ~d);
            g = (unsigned)((7 * i) & 15);
        }
        uint32_t next = d;
        d = c;
        c = b;
        b = b + ROTL32(a + f + K[i] + M[g], S[i]);
        a = next;
    }
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
}

void md5_init(md5_ctx_t *ctx) {
    ctx->state[0] = 0x67452301u;
    ctx->state[1] = 0xefcdab89u;
    ctx->state[2] = 0x98badcfeu;
    ctx->state[3] = 0x10325476u;
    ctx->bitcount = 0;
    memset(ctx->buffer, 0, sizeof ctx->buffer);
}

void md5_update(md5_ctx_t *ctx, const void *data, size_t len) {
    const uint8_t *in = (const uint8_t *)data;
    size_t used = (size_t)((ctx->bitcount >> 3) & 63);
    ctx->bitcount += (uint64_t)len << 3;
    if (used) {
        size_t take = 64 - used;
        if (take > len)
            take = len;
        memcpy(ctx->buffer + used, in, take);
        in += take;
        len -= take;
        if (used + take < 64)
            return;
        md5_transform(ctx->state, ctx->buffer);
    }
    while (len >= 64) {
        md5_transform(ctx->state, in);
        in += 64;
        len -= 64;
    }
    if (len)
        memcpy(ctx->buffer, in, len);
}

void md5_final(md5_ctx_t *ctx, uint8_t digest[16]) {
    const uint64_t bits = ctx->bitcount;
    static const uint8_t pad[64] = {0x80};
    size_t used = (size_t)((bits >> 3) & 63);
    size_t padLen = used < 56 ? 56 - used : 120 - used;
    md5_update(ctx, pad, padLen);
    uint8_t length[8];
    for (int i = 0; i < 8; ++i)
        length[i] = (uint8_t)(bits >> (8 * i));
    md5_update(ctx, length, 8);
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            digest[i * 4 + j] = (uint8_t)(ctx->state[i] >> (8 * j));
    memset(ctx, 0, sizeof *ctx);
}

void md5_hex(const void *data, size_t len, char out[33]) {
    static const char digits[] = "0123456789abcdef";
    md5_ctx_t ctx;
    uint8_t digest[16];
    md5_init(&ctx);
    md5_update(&ctx, data, len);
    md5_final(&ctx, digest);
    for (int i = 0; i < 16; ++i) {
        out[i * 2] = digits[digest[i] >> 4];
        out[i * 2 + 1] = digits[digest[i] & 15];
    }
    out[32] = '\0';
}

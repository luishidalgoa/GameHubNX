#ifndef GAMEHUBNX_MD5_H
#define GAMEHUBNX_MD5_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// MD5 (RFC 1321). Solo para comprobar que un fichero descargado es el que el
// repo publico: el md5 de HB App Store va en su repo.json. No es una
// comprobacion de seguridad frente a un atacante, es de integridad.
typedef struct {
    uint32_t state[4];
    uint64_t bitcount;
    uint8_t buffer[64];
} md5_ctx_t;

void md5_init(md5_ctx_t *ctx);
void md5_update(md5_ctx_t *ctx, const void *data, size_t len);
void md5_final(md5_ctx_t *ctx, uint8_t digest[16]);

// Digest en hexadecimal minuscula, out[33] incluido el cero final.
void md5_hex(const void *data, size_t len, char out[33]);

#ifdef __cplusplus
}
#endif

#endif

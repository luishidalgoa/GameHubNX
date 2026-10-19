// Vectores de RFC 1321 para el MD5 de src/core/md5.c.

#include "core/md5.h"

#include <cassert>
#include <cstdio>
#include <cstring>

int main() {
    struct Vector {
        const char* input;
        const char* digest;
    };
    const Vector vectors[] = {
        {"", "d41d8cd98f00b204e9800998ecf8427e"},
        {"abc", "900150983cd24fb0d6963f7d28e17f72"},
        {"The quick brown fox jumps over the lazy dog",
         "9e107d9d372bb6826bd81d3542a419d6"},
    };
    for (const Vector& v : vectors) {
        char out[33];
        md5_hex(v.input, std::strlen(v.input), out);
        assert(std::strcmp(out, v.digest) == 0);
    }

    // Trocear la entrada no cambia el resultado, tambien al cruzar el bloque de
    // 64 bytes.
    char big[300];
    for (int i = 0; i < 300; ++i)
        big[i] = static_cast<char>(i * 7);
    char oneShot[33];
    md5_hex(big, sizeof big, oneShot);
    md5_ctx_t ctx;
    uint8_t digest[16];
    md5_init(&ctx);
    for (size_t i = 0; i < sizeof big; i += 13) {
        const size_t left = sizeof big - i;
        md5_update(&ctx, big + i, left < 13 ? left : 13);
    }
    md5_final(&ctx, digest);
    char incremental[33];
    for (int i = 0; i < 16; ++i)
        std::snprintf(incremental + i * 2, 3, "%02x", digest[i]);
    assert(std::strcmp(oneShot, incremental) == 0);

    std::printf("test_md5: ok\n");
    return 0;
}

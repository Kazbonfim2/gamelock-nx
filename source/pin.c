#include "pin.h"

#include <string.h>

void pin_derive(const char *pin, const void *salt, size_t salt_size,
                u32 iterations, u8 out[PIN_HASH_SIZE])
{
    memset(out, 0, PIN_HASH_SIZE);

    if (pin == NULL || salt == NULL || salt_size > PIN_SALT_SIZE || iterations == 0) {
        return;
    }

    // PBKDF2: T_1 = U_1 xor U_2 xor ... xor U_c
    //         U_1 = HMAC(P, S || INT_BE32(1));  U_i = HMAC(P, U_{i-1})
    u8 seed[PIN_SALT_SIZE + 4];
    memcpy(seed, salt, salt_size);
    seed[salt_size + 0] = 0;
    seed[salt_size + 1] = 0;
    seed[salt_size + 2] = 0;
    seed[salt_size + 3] = 1;

    const size_t pin_len = strlen(pin);

    u8 u[PIN_HASH_SIZE];
    u8 t[PIN_HASH_SIZE];

    hmacSha256CalculateMac(u, pin, pin_len, seed, salt_size + 4);
    memcpy(t, u, sizeof(t));

    for (u32 i = 1; i < iterations; i++) {
        hmacSha256CalculateMac(u, pin, pin_len, u, sizeof(u));
        for (size_t j = 0; j < PIN_HASH_SIZE; j++) {
            t[j] ^= u[j];
        }
    }

    memcpy(out, t, PIN_HASH_SIZE);
}

void pin_random_salt(u8 salt[PIN_SALT_SIZE])
{
    randomGet(salt, PIN_SALT_SIZE);
}

bool pin_hash_equals(const u8 a[PIN_HASH_SIZE], const u8 b[PIN_HASH_SIZE])
{
    u8 diff = 0;
    for (size_t i = 0; i < PIN_HASH_SIZE; i++) {
        diff |= (u8)(a[i] ^ b[i]);
    }
    return diff == 0;
}

bool pin_digits_valid(const char *pin)
{
    if (pin == NULL) {
        return false;
    }

    const size_t len = strlen(pin);
    if (len < PIN_MIN_DIGITS || len > PIN_MAX_DIGITS) {
        return false;
    }

    for (size_t i = 0; i < len; i++) {
        if (pin[i] < '0' || pin[i] > '9') {
            return false;
        }
    }
    return true;
}

// Vetores de PBKDF2-HMAC-SHA256, P="password", S="salt", dkLen=32, do conjunto
// de test vectors do draft-josefsson (mesmos valores publicados em
// PHP-PasswordLib/test/Data/Vectors/pbkdf2-draft-josefsson-sha256.test-vectors).
// c=1 valida o HMAC; c=2 e c=4096 validam o laco de iteracoes.
bool pin_selftest(void)
{
    static const struct {
        u32 iterations;
        u8  expected[PIN_HASH_SIZE];
    } kats[] = {
        { 1, {
            0x12, 0x0f, 0xb6, 0xcf, 0xfc, 0xf8, 0xb3, 0x2c, 0x43, 0xe7, 0x22, 0x52, 0x56, 0xc4, 0xf8, 0x37,
            0xa8, 0x65, 0x48, 0xc9, 0x2c, 0xcc, 0x35, 0x48, 0x08, 0x05, 0x98, 0x7c, 0xb7, 0x0b, 0xe1, 0x7b,
        } },
        { 2, {
            0xae, 0x4d, 0x0c, 0x95, 0xaf, 0x6b, 0x46, 0xd3, 0x2d, 0x0a, 0xdf, 0xf9, 0x28, 0xf0, 0x6d, 0xd0,
            0x2a, 0x30, 0x3f, 0x8e, 0xf3, 0xc2, 0x51, 0xdf, 0xd6, 0xe2, 0xd8, 0x5a, 0x95, 0x47, 0x4c, 0x43,
        } },
        { 4096, {
            0xc5, 0xe4, 0x78, 0xd5, 0x92, 0x88, 0xc8, 0x41, 0xaa, 0x53, 0x0d, 0xb6, 0x84, 0x5c, 0x4c, 0x8d,
            0x96, 0x28, 0x93, 0xa0, 0x01, 0xce, 0x4e, 0x11, 0xa4, 0x96, 0x38, 0x73, 0xaa, 0x98, 0x13, 0x4a,
        } },
    };

    for (size_t i = 0; i < sizeof(kats) / sizeof(kats[0]); i++) {
        u8 got[PIN_HASH_SIZE];
        pin_derive("password", "salt", 4, kats[i].iterations, got);
        if (memcmp(got, kats[i].expected, PIN_HASH_SIZE) != 0) {
            return false;
        }
    }

    // Sanidade da comparacao em tempo constante.
    u8 a[PIN_HASH_SIZE], b[PIN_HASH_SIZE];
    memset(a, 0xA5, sizeof(a));
    memcpy(b, a, sizeof(b));
    if (!pin_hash_equals(a, b)) {
        return false;
    }
    b[PIN_HASH_SIZE - 1] ^= 0x01;
    if (pin_hash_equals(a, b)) {
        return false;
    }

    return pin_digits_valid("1234") && !pin_digits_valid("12") && !pin_digits_valid("12a4");
}

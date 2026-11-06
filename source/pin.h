// GameLock - Etapa 3: derivacao e verificacao do PIN.
//
// PBKDF2-HMAC-SHA256 (RFC 2898) construido sobre:
//   hmacSha256CalculateMac   libnx crypto/hmac.h:41
//   randomGet                libnx kernel/random.h:15
//
// O PIN em texto nunca e gravado: so salt, iteracoes e o hash derivado.
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include <switch.h>

#define PIN_SALT_SIZE  16
#define PIN_HASH_SIZE  32
#define PIN_ITERATIONS 20000
#define PIN_MIN_DIGITS 4
#define PIN_MAX_DIGITS 8

// PBKDF2-HMAC-SHA256 com dkLen = PIN_HASH_SIZE (exatamente um bloco de saida).
// salt_size precisa ser <= PIN_SALT_SIZE.
void pin_derive(const char *pin, const void *salt, size_t salt_size,
                u32 iterations, u8 out[PIN_HASH_SIZE]);

void pin_random_salt(u8 salt[PIN_SALT_SIZE]);

// Comparacao em tempo constante (nao vaza em qual byte os hashes diferem).
bool pin_hash_equals(const u8 a[PIN_HASH_SIZE], const u8 b[PIN_HASH_SIZE]);

// true se o PIN tem entre PIN_MIN_DIGITS e PIN_MAX_DIGITS digitos decimais.
bool pin_digits_valid(const char *pin);

// Known-answer tests do PBKDF2-HMAC-SHA256 (c=1, c=2 e c=4096).
// Roda na inicializacao: se der falso, a derivacao esta quebrada.
bool pin_selftest(void);

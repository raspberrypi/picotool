#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#undef MBEDTLS_ECDSA_DETERMINISTIC

#include <stdint.h>
#include <stdlib.h>
#include <assert.h>

#include <mbedtls/sha256.h>
#include <mbedtls/ecdsa.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/pk.h>
#include <mbedtls/ecp.h>
#include <mbedtls/aes.h>

#ifdef __cplusplus
#define _Static_assert static_assert
#endif

typedef struct signature {
    /** An array 64 bytes making up 2 256-bit values. */
    uint8_t bytes[64];
    uint8_t der[MBEDTLS_ECDSA_MAX_LEN];
    size_t der_len;
} signature_t; /**< Convenience typedef */

typedef struct message_digest {
    /** An array 32 bytes making up the 256-bit value. */
    uint8_t bytes[32];
} message_digest_t; /**< Convenience typedef */

typedef struct iv {
    /** An array 16 bytes random data. */
    uint8_t bytes[16];
} iv_t; /**< Convenience typedef */

typedef signature_t public_t;
typedef message_digest_t private_t;

void mb_sha256_buffer(const uint8_t *data, size_t len, message_digest_t *digest_out);
void mb_aes256_buffer(const uint8_t *data, size_t len, uint8_t *data_out, const private_t *key, iv_t *iv);
void mb_sign_sha256(const uint8_t *entropy, size_t entropy_size, const message_digest_t *m, const public_t *p, const private_t *d, signature_t *out);

uint32_t mb_verify_signature_secp256k1(
        signature_t signature[1],
        const public_t public_key[1],
        const message_digest_t digest[1]);

#define sha256_buffer mb_sha256_buffer
#define aes256_buffer mb_aes256_buffer
#define sign_sha256 mb_sign_sha256
#define verify_signature_secp256k1 mb_verify_signature_secp256k1

#ifdef __cplusplus
};
#endif

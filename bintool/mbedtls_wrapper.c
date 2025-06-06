#include "mbedtls_wrapper.h"
#include <string.h>
#include <stdio.h>
#include <time.h>

#if ENABLE_DEBUG_LOG
#define DEBUG_LOG(...) printf(__VA_ARGS__)
static void dump_buf(const char *title, const unsigned char *buf, size_t len)
{
    size_t i;

    DEBUG_LOG("%s", title);
    for (i = 0; i < len; i++) {
        DEBUG_LOG("%c%c", "0123456789ABCDEF" [buf[i] / 16],
                       "0123456789ABCDEF" [buf[i] % 16]);
    }
    DEBUG_LOG("\n");
}

static void dump_pubkey(const char *title, mbedtls_ecdsa_context *key)
{
    unsigned char buf[300];
    size_t len;

    if (mbedtls_ecp_point_write_binary(&key->grp, &key->Q,
                                       MBEDTLS_ECP_PF_UNCOMPRESSED, &len, buf, sizeof(buf)) != 0) {
        DEBUG_LOG("internal error\n");
        return;
    }

    dump_buf(title, buf, len);
}
#else
#define DEBUG_LOG(...) ((void)0)
#define dump_buf(...) ((void)0)
#define dump_pubkey(...) ((void)0)
#endif

void mb_sha256_buffer(const uint8_t *data, size_t len, message_digest_t *digest_out) {
    mbedtls_sha256(data, len, digest_out->bytes, 0);
}

#if IV0_XOR
// Taken from mbedtls_aes_crypt_ctr, but with XOR instead of adding to IV0
int mb_aes_crypt_ctr_xor(mbedtls_aes_context *ctx,
    size_t length,
    unsigned char iv0[16],
    unsigned char nonce_xor[16],
    unsigned char stream_block[16],
    const unsigned char *input,
    unsigned char *output)
{
    int c;
    int ret = 0;
    size_t n = 0;
    uint32_t counter = 0;

    assert(length == (uint32_t)length);

    while (length--) {
        if (n == 0) {
            for (int i = 16; i > 0; i--) {
                nonce_xor[i-1] = iv0[i-1];
                if (i > 16 - sizeof(counter)) {
                    nonce_xor[i-1] ^= (unsigned char)(counter >> ((16-i)*8));
                }
            }

            ret = mbedtls_aes_crypt_ecb(ctx, MBEDTLS_AES_ENCRYPT, nonce_xor, stream_block);
            if (ret != 0) {
                break;
            }
            counter++;
        }
        c = *input++;
        *output++ = (unsigned char) (c ^ stream_block[n]);

        n = (n + 1) & 0x0F;
    }

    return ret;
}
#endif

void mb_aes256_buffer(const uint8_t *data, size_t len, uint8_t *data_out, const aes_key_t *key, iv_t *iv) {
    mbedtls_aes_context aes;

    assert(len % 16 == 0);

    mbedtls_aes_setkey_enc(&aes, key->bytes, 256);
    uint8_t stream_block[16] = {0};
    size_t nc_off = 0;
#if IV0_XOR
    uint8_t xor_working_block[16] = {0};
    mb_aes_crypt_ctr_xor(&aes, len, iv->bytes, xor_working_block, stream_block, data, data_out);
#else
    mbedtls_aes_crypt_ctr(&aes, len, &nc_off, iv->bytes, stream_block, data, data_out);
#endif
}

void raw_to_der(signature_t *sig) {
    // todo make this der - currently ber
    unsigned char r[33];
    r[0] = 0;
    memcpy(r+1, sig->bytes, 32);
    unsigned char s[33];
    s[0] = 0;
    memcpy(s+1, sig->bytes + 32, 32);

    int8_t r_len_dec = 0;
    if (r[1] & 0x80) {
        // Needs padding
        r_len_dec = -1;
    } else {
        for (int i=1; i < 32; i++) {
            if (r[i] != 0) {
                break;
            }
            r_len_dec++;
        }
    }

    int8_t s_len_dec = 0;
    if (s[1] & 0x80) {
        // Needs padding
        s_len_dec = -1;
    } else {
        for (int i=1; i < 32; i++) {
            if (s[i] != 0) {
                break;
            }
            s_len_dec++;
        }
    }

    // Write it out
    sig->der[0] = 0x30;
    sig->der[1] = 68 - r_len_dec - s_len_dec;
    sig->der[2] = 0x02;
    sig->der[3] = 32 - r_len_dec;
    uint8_t b2 = sig->der[3];
    memcpy(sig->der + 4, r + 1 + r_len_dec, b2);
    sig->der[4 + b2] = 0x02;
    sig->der[5 + b2] = 32 - s_len_dec;
    uint8_t b3 = sig->der[5 + b2];
    memcpy(sig->der + 6 + b2, s + 1 + s_len_dec, b3);


    sig->der_len = 6 + b2 + b3;
}


void der_to_raw(signature_t *sig) {
    assert(sig->der[0] == 0x30);
    assert(sig->der[2] == 0x02);
    uint8_t b2 = sig->der[3];
    assert(sig->der[4 + b2] == 0x02);
    uint8_t b3 = sig->der[5 + b2];

    assert(sig->der_len == 6u + b2 + b3);

    unsigned char r[32];
    if (b2 == 33) {
        memcpy(r, sig->der + 4 + 1, 32);
    } else if (b2 == 32) {
        memcpy(r, sig->der + 4, 32);
    } else {
        memset(r, 0, sizeof(r));
        memcpy(r + (32 - b2), sig->der + 4, (32 - b2));
    }

    unsigned char s[32];
    if (b3 == 33) {
        memcpy(s, sig->der + 6 + b2 + 1, 32);
    } else if (b3 == 32) {
        memcpy(s, sig->der + 6 + b2, 32);
    } else {
        memset(s, 0, sizeof(r));
        memcpy(s + (32 - b3), sig->der + 6 + b2, (32 - b3));
    }

    memset(sig->bytes, 0, sizeof(sig->bytes));
    memcpy(sig->bytes, r, sizeof(r));
    memcpy(sig->bytes + 32, s, sizeof(s));
}


void mb_sign_sha256(const uint8_t *entropy, size_t entropy_size, const message_digest_t *m, const public_t *p, const private_t *d, signature_t *out) {
    int ret = 1;
    mbedtls_ecdsa_context ctx_sign;
    mbedtls_entropy_context entropy_ctx;
    mbedtls_ctr_drbg_context ctr_drbg;

    mbedtls_ecdsa_init(&ctx_sign);
    mbedtls_ctr_drbg_init(&ctr_drbg);

    memset(out->der, 0, sizeof(out->der));

    DEBUG_LOG("\n  . Seeding the random number generator...");
    fflush(stdout);

    mbedtls_entropy_init(&entropy_ctx);
    // mbedtls_entropy_update_manual(&entropy, entropy_in, entropy_size);
    if ((ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy_ctx,
                                     (const unsigned char *) entropy,
                                     entropy_size)) != 0) {
        DEBUG_LOG(" failed\n  ! mbedtls_ctr_drbg_seed returned %d\n", ret);
        return;
    }

    DEBUG_LOG(" ok\n");

    DEBUG_LOG("  . Loading key pair...");
    fflush(stdout);

    mbedtls_ecp_group_load(&ctx_sign.grp, MBEDTLS_ECP_DP_SECP256K1);
    mbedtls_mpi_read_binary(&ctx_sign.d, (unsigned char*)d, 32);
    mbedtls_mpi_read_binary(&ctx_sign.Q.X, (unsigned char*)p, 32);
    mbedtls_mpi_read_binary(&ctx_sign.Q.Y, (unsigned char*)p + 32, 32);
    // Z must be 1
    mbedtls_mpi_add_int(&ctx_sign.Q.Z, &ctx_sign.Q.Z, 1);

    DEBUG_LOG(" ok (key size: %d bits)\n", (int) ctx_sign.grp.pbits);

#if MBEDTLS_VERSION_MAJOR >= 3
    ret = mbedtls_ecp_check_pub_priv(&ctx_sign, &ctx_sign, mbedtls_ctr_drbg_random, &ctr_drbg);
#else
    ret = mbedtls_ecp_check_pub_priv(&ctx_sign, &ctx_sign);
#endif
    DEBUG_LOG("Pub Priv Returned %d\n", ret);

    dump_pubkey("  + Public key: ", &ctx_sign);

    dump_buf("  + Hash: ", m->bytes, sizeof(m->bytes));

    DEBUG_LOG("  . Signing message hash...");
    fflush(stdout);

    if ((ret = mbedtls_ecdsa_write_signature(&ctx_sign, MBEDTLS_MD_SHA256,
                                             m->bytes, sizeof(m->bytes),
                                             out->der,
                                             #if MBEDTLS_VERSION_MAJOR >= 3
                                             sizeof(out->der),
                                             #endif
                                             &out->der_len,
                                             mbedtls_ctr_drbg_random, &ctr_drbg)) != 0) {
        DEBUG_LOG(" failed\n  ! mbedtls_ecdsa_write_signature returned %d\n", ret);
        return;
    }
    DEBUG_LOG(" ok (signature length = %u)\n", (unsigned int) out->der_len);

    dump_buf("  + DER Signature: ", out->der, out->der_len);

    // Populate raw signature value from der
    der_to_raw(out);

    dump_buf("  + Raw Signature: ", (unsigned char*)out, 64);
}

uint32_t mb_verify_signature_secp256k1(
        signature_t signature[1],
        const public_t public_key[1],
        const message_digest_t digest[1]) {

    int ret = 1;
    mbedtls_ecdsa_context ctx_verify;
    unsigned char hash[32];
    memcpy(hash, digest, sizeof(hash));
    if (signature->der_len == 0) {
        raw_to_der(signature);
    }

    mbedtls_ecdsa_init(&ctx_verify);

    mbedtls_ecp_group_load(&ctx_verify.grp, MBEDTLS_ECP_DP_SECP256K1);
    mbedtls_mpi_read_binary(&ctx_verify.Q.X, public_key->bytes, 32);
    mbedtls_mpi_read_binary(&ctx_verify.Q.Y, public_key->bytes + 32, 32);
    // Z must be 1
    mbedtls_mpi_add_int(&ctx_verify.Q.Z, &ctx_verify.Q.Z, 1);

    /*
     * Verify signature
     */
    DEBUG_LOG("  . Verifying signature...");
    fflush(stdout);

    if ((ret = mbedtls_ecdsa_read_signature(&ctx_verify,
                                            hash, sizeof(hash),
                                            signature->der, signature->der_len)) != 0) {
        DEBUG_LOG(" failed\n  ! mbedtls_ecdsa_read_signature returned -%x\n", -ret);
        return 1;
    }

    DEBUG_LOG(" ok\n");

    return 0;
}

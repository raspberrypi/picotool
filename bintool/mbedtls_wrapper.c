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

#define assert_or_return(assertion, retval) assert(assertion); if (!(assertion)) return retval;

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

// Write a 32-byte big-endian unsigned value as a minimal DER INTEGER, returning the number of bytes written
static size_t write_der_integer(uint8_t *out, const uint8_t *in) {
    // Strip leading zeros, but always keep at least one byte
    size_t skip = 0;
    while (skip < 31 && in[skip] == 0) {
        skip++;
    }
    // Pad with a zero byte if the top bit is set, so it isn't read as negative
    size_t pad = (in[skip] & 0x80) ? 1 : 0;
    size_t len = 32 - skip + pad;

    out[0] = 0x02;
    out[1] = (uint8_t)len;
    out[2] = 0;
    memcpy(out + 2 + pad, in + skip, 32 - skip);
    return 2 + len;
}

// Read a DER INTEGER of up to 32 bytes (plus optional zero padding byte) into a 32-byte big-endian value
static bool read_der_integer(const uint8_t *in, size_t avail, uint8_t *out, size_t *consumed) {
    if (avail < 2 || in[0] != 0x02) return false;
    size_t len = in[1];
    if (len == 0 || len > 33 || 2 + len > avail) return false;
    *consumed = 2 + len;

    const uint8_t *p = in + 2;
    if (len == 33) {
        // Only valid if the extra byte is zero padding
        if (p[0] != 0) return false;
        p++;
        len--;
    }
    memset(out, 0, 32);
    memcpy(out + (32 - len), p, len);
    return true;
}

void raw_to_der(signature_t *sig) {
    size_t len = write_der_integer(sig->der + 2, sig->bytes);
    len += write_der_integer(sig->der + 2 + len, sig->bytes + 32);

    // Max length is 2 * 35 = 70, so short-form length is always sufficient
    sig->der[0] = 0x30;
    sig->der[1] = (uint8_t)len;
    sig->der_len = 2 + len;
}


bool der_to_raw(signature_t *sig) {
    if (sig->der_len < 2 || sig->der_len > sizeof(sig->der)) return false;
    // SEQUENCE, with short-form length covering the rest of the buffer
    if (sig->der[0] != 0x30 || sig->der[1] & 0x80 || sig->der[1] != sig->der_len - 2) return false;

    uint8_t r[32];
    uint8_t s[32];
    size_t r_len, s_len;
    if (!read_der_integer(sig->der + 2, sig->der_len - 2, r, &r_len)) return false;
    if (!read_der_integer(sig->der + 2 + r_len, sig->der_len - 2 - r_len, s, &s_len)) return false;
    if (2 + r_len + s_len != sig->der_len) return false;

    memcpy(sig->bytes, r, sizeof(r));
    memcpy(sig->bytes + 32, s, sizeof(s));

    return true;
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

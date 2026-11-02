#include "../core/management_crypto.h"

#if defined(ESP_PLATFORM) && !defined(GS232B_HOST_CRYPTO)
#include "esp_app_desc.h"
#include "psa/crypto.h"
#else
#include <openssl/rand.h>
#include <openssl/sha.h>
#endif

void gs232b_management_clear(void *bytes, size_t size)
{
    volatile uint8_t *out = bytes;
    while (size-- != 0) *out++ = 0;
}

gs232b_error_t gs232b_management_token(char token[65])
{
    uint8_t random[32];
#if defined(ESP_PLATFORM) && !defined(GS232B_HOST_CRYPTO)
    if (psa_crypto_init() != PSA_SUCCESS ||
        psa_generate_random(random, sizeof(random)) != PSA_SUCCESS) {
        gs232b_management_clear(random, sizeof(random));
        return GS232B_ERROR_HARDWARE_FAULT;
    }
#else
    if (RAND_bytes(random, sizeof(random)) != 1) {
        gs232b_management_clear(random, sizeof(random));
        return GS232B_ERROR_HARDWARE_FAULT;
    }
#endif
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < sizeof(random); ++i) {
        token[i * 2U] = hex[random[i] >> 4];
        token[i * 2U + 1U] = hex[random[i] & 15U];
    }
    token[64] = '\0';
    gs232b_management_clear(random, sizeof(random));
    return GS232B_OK;
}

gs232b_error_t gs232b_management_digest(const uint8_t *bytes, size_t size, uint8_t digest[32])
{
#if defined(ESP_PLATFORM) && !defined(GS232B_HOST_CRYPTO)
    size_t written = 0;
    return psa_crypto_init() == PSA_SUCCESS &&
        psa_hash_compute(PSA_ALG_SHA_256, bytes, size, digest, 32, &written) == PSA_SUCCESS &&
        written == 32 ? GS232B_OK : GS232B_ERROR_HARDWARE_FAULT;
#else
    return SHA256(bytes, size, digest) != NULL ? GS232B_OK : GS232B_ERROR_HARDWARE_FAULT;
#endif
}

const char *gs232b_management_version(void)
{
#if defined(ESP_PLATFORM) && !defined(GS232B_HOST_CRYPTO)
    return esp_app_get_description()->version;
#else
    return "host-test";
#endif
}

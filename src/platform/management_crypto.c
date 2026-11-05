/*
 * GS-232B Open - rotator controller firmware.
 * Copyright (C) 2026 BG5GEB <bg5geb@misakanet.team>
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "../core/management_crypto.h"

#include <limits.h>

#if defined(ESP_PLATFORM) && !defined(GS232B_HOST_CRYPTO)
#include "esp_app_desc.h"
#include "psa/crypto.h"
#else
#include <openssl/evp.h>
#include <openssl/rand.h>
#endif

void gs232b_management_clear(void *bytes, size_t size)
{
    volatile uint8_t *out = bytes;
    while (size-- != 0) *out++ = 0;
}

gs232b_error_t gs232b_management_random(uint8_t *bytes, size_t size)
{
    if (bytes == NULL || size == 0) return GS232B_ERROR_INVALID_ARGUMENT;
#if defined(ESP_PLATFORM) && !defined(GS232B_HOST_CRYPTO)
    if (psa_crypto_init() != PSA_SUCCESS ||
        psa_generate_random(bytes, size) != PSA_SUCCESS) {
        gs232b_management_clear(bytes, size);
        return GS232B_ERROR_HARDWARE_FAULT;
    }
#else
    if (size > INT_MAX || RAND_bytes(bytes, (int)size) != 1) {
        gs232b_management_clear(bytes, size);
        return GS232B_ERROR_HARDWARE_FAULT;
    }
#endif
    return GS232B_OK;
}

gs232b_error_t gs232b_management_derive_password(const uint8_t *password, size_t password_size,
    const uint8_t salt[GS232B_ADMIN_SALT_SIZE], uint8_t verifier[GS232B_ADMIN_VERIFIER_SIZE])
{
    if (password == NULL || password_size == 0 || password_size > 64 || salt == NULL || verifier == NULL) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
#if defined(ESP_PLATFORM) && !defined(GS232B_HOST_CRYPTO)
    psa_key_derivation_operation_t operation = PSA_KEY_DERIVATION_OPERATION_INIT;
    psa_status_t status = psa_crypto_init();
    if (status == PSA_SUCCESS) {
        status = psa_key_derivation_setup(&operation,
            PSA_ALG_PBKDF2_HMAC(PSA_ALG_SHA_256));
    }
    if (status == PSA_SUCCESS) {
        status = psa_key_derivation_input_bytes(&operation,
            PSA_KEY_DERIVATION_INPUT_PASSWORD, password, password_size);
    }
    if (status == PSA_SUCCESS) {
        status = psa_key_derivation_input_bytes(&operation,
            PSA_KEY_DERIVATION_INPUT_SALT, salt, GS232B_ADMIN_SALT_SIZE);
    }
    if (status == PSA_SUCCESS) {
        status = psa_key_derivation_input_integer(&operation,
            PSA_KEY_DERIVATION_INPUT_COST, UINT32_C(20000));
    }
    if (status == PSA_SUCCESS) {
        status = psa_key_derivation_output_bytes(&operation, verifier, GS232B_ADMIN_VERIFIER_SIZE);
    }
    (void)psa_key_derivation_abort(&operation);
    if (status != PSA_SUCCESS) {
        gs232b_management_clear(verifier, GS232B_ADMIN_VERIFIER_SIZE);
        return GS232B_ERROR_HARDWARE_FAULT;
    }
    return GS232B_OK;
#else
    if (password_size > INT_MAX) return GS232B_ERROR_INVALID_ARGUMENT;
    return PKCS5_PBKDF2_HMAC((const char *)password, (int)password_size, salt,
        GS232B_ADMIN_SALT_SIZE, 20000, EVP_sha256(), GS232B_ADMIN_VERIFIER_SIZE,
        verifier) == 1 ? GS232B_OK : GS232B_ERROR_HARDWARE_FAULT;
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

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

#include "wifi_sta_test_support.h"

static bool credential_modes(void)
{
    for (gs232b_wifi_sta_security_t mode = GS232B_WIFI_STA_SECURITY_AUTO;
         mode <= GS232B_WIFI_STA_SECURITY_WPA3_PERSONAL; ++mode) {
        gs232b_wifi_sta_config_t config = wifi_good;
        config.security = mode;
        CHECK(gs232b_wifi_sta_config_valid(&config));
        config.password[0] = 0;
        CHECK(gs232b_wifi_sta_config_valid(&config) == (mode == GS232B_WIFI_STA_SECURITY_AUTO));
        strcpy(config.password, "short");
        CHECK(gs232b_wifi_sta_config_valid(&config) == (mode == GS232B_WIFI_STA_SECURITY_WPA3_PERSONAL));
        memset(config.password, 'a', 64); config.password[64] = 0;
        CHECK(gs232b_wifi_sta_config_valid(&config) == (mode != GS232B_WIFI_STA_SECURITY_WPA3_PERSONAL));
        config.password[63] = 'z';
        CHECK(!gs232b_wifi_sta_config_valid(&config));
        config.password[63] = 0;
        CHECK(gs232b_wifi_sta_config_valid(&config));
        config.password[2] = '\n';
        CHECK(!gs232b_wifi_sta_config_valid(&config));
    }
    gs232b_wifi_sta_config_t invalid = wifi_good;
    invalid.security = (gs232b_wifi_sta_security_t)99;
    CHECK(!gs232b_wifi_sta_config_valid(&invalid));
    return true;
}

static bool sdk_security_mapping(void)
{
    const wifi_auth_mode_t thresholds[] = {
        WIFI_AUTH_WPA_PSK, WIFI_AUTH_WPA_PSK, WIFI_AUTH_WPA2_PSK, WIFI_AUTH_WPA3_PSK
    };
    wifi_fixture_t fixture;
    CHECK(wifi_open(&fixture, &wifi_good));
    for (gs232b_wifi_sta_security_t mode = GS232B_WIFI_STA_SECURITY_AUTO;
         mode <= GS232B_WIFI_STA_SECURITY_WPA3_PERSONAL; ++mode) {
        gs232b_wifi_sta_config_t config = wifi_good;
        config.security = mode;
        CHECK(gs232b_esp_wifi_sta_configure(&fixture.sta, &config) == GS232B_OK);
        CHECK(wifi_settle(&fixture));
        const wifi_sta_config_t sdk = fake_wifi_status().credentials.sta;
        CHECK(sdk.threshold.authmode == thresholds[mode]);
        CHECK(sdk.pmf_cfg.required == (mode == GS232B_WIFI_STA_SECURITY_WPA3_PERSONAL));
        CHECK(sdk.sae_pwe_h2e == WPA3_SAE_PWE_BOTH);
        CHECK(sdk.sae_pk_mode == WPA3_SAE_PK_MODE_DISABLED);
        CHECK(sdk.sort_method == WIFI_CONNECT_AP_BY_SECURITY);
        CHECK(memcmp(sdk.password, config.password, strlen(config.password)) == 0);
    }
    gs232b_wifi_sta_config_t open = {.enabled = true, .ssid = "guest"};
    CHECK(gs232b_esp_wifi_sta_configure(&fixture.sta, &open) == GS232B_OK);
    CHECK(wifi_settle(&fixture));
    CHECK(fake_wifi_status().credentials.sta.threshold.authmode == WIFI_AUTH_OPEN);
    CHECK(!fake_wifi_status().credentials.sta.pmf_cfg.required);
    CHECK(wifi_close(&fixture));
    return true;
}

bool wifi_test_security(void)
{
    return credential_modes() && sdk_security_mapping();
}

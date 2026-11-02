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

#ifndef TEST_ADC_TYPES_H
#define TEST_ADC_TYPES_H

typedef enum { ADC_UNIT_1, ADC_UNIT_2 } adc_unit_t;
typedef enum { ADC_CHANNEL_0, ADC_CHANNEL_1 } adc_channel_t;
typedef enum { ADC_ATTEN_DB_0, ADC_ATTEN_DB_2_5, ADC_ATTEN_DB_6,
               ADC_ATTEN_DB_12 } adc_atten_t;
typedef enum { ADC_BITWIDTH_DEFAULT, ADC_BITWIDTH_12 = 12 } adc_bitwidth_t;
typedef enum { ADC_ULP_MODE_DISABLE } adc_ulp_mode_t;
typedef int adc_oneshot_clk_src_t;

#endif

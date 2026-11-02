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

#ifndef TEST_FREERTOS_TASK_H
#define TEST_FREERTOS_TASK_H
#include "freertos/FreeRTOS.h"
typedef void (*TaskFunction_t)(void *);
typedef struct fake_task *TaskHandle_t;
BaseType_t xTaskCreate(TaskFunction_t entry, const char *name, uint32_t stack_size,
    void *ctx, UBaseType_t priority, TaskHandle_t *task);
void vTaskDelay(TickType_t ticks);
void vTaskDelete(TaskHandle_t task);
#endif

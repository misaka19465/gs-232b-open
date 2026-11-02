#ifndef GS232B_MANAGEMENT_CRYPTO_H
#define GS232B_MANAGEMENT_CRYPTO_H

#include "gs232b/types.h"
#include <stddef.h>

/** @brief 使用平台 CSPRNG 生成 256bit token（64 个十六进制字符及 NUL）。 */
gs232b_error_t gs232b_management_token(char token[65]);
/** @brief 使用平台 SHA-256 计算 token 校验材料，失败时拒绝认证。 */
gs232b_error_t gs232b_management_digest(const uint8_t *bytes, size_t size, uint8_t digest[32]);
/** @brief 清除临时凭据，禁止优化器省略擦除。 */
void gs232b_management_clear(void *bytes, size_t size);
/** @brief 返回实际固件构建版本；宿主测试使用独立标识。 */
const char *gs232b_management_version(void);

#endif

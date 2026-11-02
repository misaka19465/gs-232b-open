#ifndef GS232B_ESP_WS_INTERNAL_H
#define GS232B_ESP_WS_INTERNAL_H

#include "gs232b/esp_ws.h"

/** @brief HTTPD 专用：为 Upgrade 保留槽并核对本机接口。 */
esp_err_t gs232b_ws_pre_handshake(httpd_req_t *request);
/** @brief HTTPD 专用：等待控制任务有界地建立会话和欢迎语。 */
esp_err_t gs232b_ws_post_handshake(httpd_req_t *request);
/** @brief HTTPD 专用：消费一个 RFC 帧，控制帧绝不进入 RX 队列。 */
esp_err_t gs232b_ws_receive(httpd_req_t *request);
/** @brief HTTPD 专用：只登记物理关闭，生命周期覆盖至控制任务回收。 */
void gs232b_ws_disconnected(void *ctx);
/** @brief HTTPD 专用：SDK 帧编码写入固定 wire 缓冲，不调用 socket send。 */
int gs232b_ws_encode_send(httpd_handle_t server, int fd, const char *bytes,
                           size_t size, int flags);
/** @brief HTTPD 专用：发送一次有限工作，处理网络心跳和异步关闭。 */
void gs232b_ws_pump(void *ctx);
/** @brief HTTPD 专用：加入有界控制帧队列，溢出时拒绝。 */
bool gs232b_ws_control(gs232b_ws_client_t *client, httpd_ws_type_t type,
                        const uint8_t *bytes, size_t size);
/** @brief HTTPD 专用：尽力发送 Close 后标记关闭，不等待网络。 */
void gs232b_ws_fail(gs232b_ws_client_t *client, uint16_t code);
esp_err_t gs232b_provisioning_page(httpd_req_t *request);

#endif

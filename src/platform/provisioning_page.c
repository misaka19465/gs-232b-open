#include "esp_http_server.h"

#if defined(GS232B_TEST_ASSETS)
/* NOTE: 宿主机由 CMake 从同一 HTML 模板生成字节数组，验证真实页面响应。 */
#include "provisioning_html.h"
#else
/* NOTE: ESP-IDF EMBED_FILES 将独立模板链接到固件，缺失资源时应链接失败。 */
extern const unsigned char _binary_provisioning_html_gz_start[];
extern const unsigned char _binary_provisioning_html_gz_end[];
#endif

esp_err_t gs232b_provisioning_page(httpd_req_t *request)
{
#if defined(GS232B_TEST_ASSETS)
    const char *page = (const char *)provisioning_html;
    const size_t size = sizeof(provisioning_html);
#else
    const char *page = (const char *)_binary_provisioning_html_gz_start;
    const size_t size = (size_t)(_binary_provisioning_html_gz_end -
        _binary_provisioning_html_gz_start);
#endif
    esp_err_t error = httpd_resp_set_type(request, "text/html; charset=utf-8");
    if (error != ESP_OK) { return error; }
    error = httpd_resp_set_hdr(request, "Content-Encoding", "gzip");
    if (error != ESP_OK) { return error; }
    return httpd_resp_send(request, page, (ssize_t)size);
}

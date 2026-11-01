#include "ethernet_api.h"
#include "gs232b/esp_wifi_sta.h"
#include "gs232b/esp_w5500.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "netif_fake.h"

#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

ESP_EVENT_DEFINE_BASE(ETH_EVENT);
ESP_EVENT_DEFINE_BASE(IP_EVENT);
ESP_EVENT_DEFINE_BASE(WIFI_EVENT);

struct fake_eth_driver { bool started; unsigned references; gs232b_shared_spi_t *bus; };
struct fake_eth_glue {
    esp_eth_handle_t driver;
    esp_netif_t *netif;
    esp_event_handler_instance_t handler;
};
struct fake_event_handler {
    struct fake_event_handler *next;
    esp_event_base_t base;
    int32_t id;
    esp_event_handler_t callback;
    void *ctx;
};
typedef struct event {
    struct event *next;
    esp_event_base_t base;
    int32_t id;
    _Alignas(max_align_t) unsigned char data[64];
} event_t;
typedef struct { unsigned at; esp_err_t error; } fault_t;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t events_lock = PTHREAD_MUTEX_INITIALIZER;
static fake_eth_status_t status;
static fault_t faults[ETH_FAKE_CALLS];
static atomic_uint_fast64_t now_us;
static struct fake_event_handler *handlers;
static event_t *events_head, *events_tail;
static gs232b_shared_spi_t *factory_bus;
static pthread_mutex_t pause_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t pause_cond = PTHREAD_COND_INITIALIZER;
static int paused_call = -1;
static bool paused, pause_released;

static esp_err_t call(fake_eth_call_t which)
{
    assert(pthread_mutex_lock(&pause_lock) == 0);
    if (paused_call == (int)which) {
        paused = true;
        assert(pthread_cond_broadcast(&pause_cond) == 0);
        while (!pause_released) { assert(pthread_cond_wait(&pause_cond, &pause_lock) == 0); }
        paused_call = -1;
    }
    assert(pthread_mutex_unlock(&pause_lock) == 0);
    assert(pthread_mutex_lock(&lock) == 0);
    ++status.calls[which];
    esp_err_t error = status.calls[which] == faults[which].at ? faults[which].error : ESP_OK;
    assert(pthread_mutex_unlock(&lock) == 0);
    return error;
}

void fake_eth_reset(void)
{
    assert(status.live_tasks == 0 && status.live_handlers == 0 && events_head == NULL);
    memset(&status, 0, sizeof(status));
    memset(faults, 0, sizeof(faults));
    const uint8_t mac[] = {0x26, 0x6f, 0x28, 0x12, 0x34, 0x57};
    memcpy(status.official_mac, mac, sizeof(mac));
    atomic_store(&now_us, 0);
    paused_call = -1; paused = pause_released = false;
}

void fake_eth_pause(fake_eth_call_t which)
{
    assert(pthread_mutex_lock(&pause_lock) == 0);
    paused_call = (int)which; paused = pause_released = false;
    assert(pthread_mutex_unlock(&pause_lock) == 0);
}

bool fake_eth_paused(void)
{
    assert(pthread_mutex_lock(&pause_lock) == 0);
    const bool result = paused;
    assert(pthread_mutex_unlock(&pause_lock) == 0);
    return result;
}

void fake_eth_release(void)
{
    assert(pthread_mutex_lock(&pause_lock) == 0);
    pause_released = true;
    assert(pthread_cond_broadcast(&pause_cond) == 0);
    assert(pthread_mutex_unlock(&pause_lock) == 0);
}

fake_eth_status_t fake_eth_status(void)
{
    assert(pthread_mutex_lock(&lock) == 0);
    const fake_eth_status_t copy = status;
    assert(pthread_mutex_unlock(&lock) == 0);
    return copy;
}

void fake_eth_fail(fake_eth_call_t which, unsigned after, esp_err_t error)
{
    assert(pthread_mutex_lock(&lock) == 0);
    faults[which] = (fault_t){status.calls[which] + after + 1, error};
    assert(pthread_mutex_unlock(&lock) == 0);
}

void fake_eth_fail_allocation(unsigned at, bool task)
{
    status.fail_allocation = at;
    status.fail_task = task;
}

bool fake_eth_allocation(void)
{
    assert(pthread_mutex_lock(&lock) == 0);
    bool ok = ++status.allocations != status.fail_allocation;
    assert(pthread_mutex_unlock(&lock) == 0);
    return ok;
}

void fake_eth_resource(int queues, int semaphores, int tasks)
{
    assert(pthread_mutex_lock(&lock) == 0);
    status.live_queues += queues;
    status.live_semaphores += semaphores;
    status.live_tasks += tasks;
    assert(pthread_mutex_unlock(&lock) == 0);
}

int64_t esp_timer_get_time(void) { return (int64_t)atomic_load(&now_us); }
void fake_eth_advance(uint64_t us) { atomic_fetch_add(&now_us, us); }

esp_err_t esp_read_mac(uint8_t *mac, esp_mac_type_t type)
{
    assert(type == ESP_MAC_ETH);
    esp_err_t error = call(ETH_FAKE_READ_MAC);
    if (error == ESP_OK) { memcpy(mac, status.official_mac, 6); }
    return error;
}

esp_err_t esp_netif_init(void) { return ESP_OK; }
esp_err_t esp_event_loop_create_default(void) { return ESP_ERR_INVALID_STATE; }

esp_err_t esp_event_handler_instance_register(esp_event_base_t base, int32_t id,
    esp_event_handler_t callback, void *ctx, esp_event_handler_instance_t *instance)
{
    esp_err_t error = call(ETH_FAKE_REGISTER);
    if (error != ESP_OK) { return error; }
    *instance = calloc(1, sizeof(**instance));
    assert(*instance != NULL && pthread_mutex_lock(&events_lock) == 0);
    **instance = (struct fake_event_handler){NULL, base, id, callback, ctx};
    struct fake_event_handler **tail = &handlers;
    while (*tail != NULL) { tail = &(*tail)->next; }
    *tail = *instance;
    assert(pthread_mutex_unlock(&events_lock) == 0);
    assert(pthread_mutex_lock(&lock) == 0);
    ++status.live_handlers;
    assert(pthread_mutex_unlock(&lock) == 0);
    return ESP_OK;
}

esp_err_t esp_event_handler_instance_unregister(esp_event_base_t base, int32_t id,
    esp_event_handler_instance_t instance)
{
    esp_err_t error = call(ETH_FAKE_UNREGISTER);
    if (error != ESP_OK) { return error; }
    assert(pthread_mutex_lock(&events_lock) == 0);
    struct fake_event_handler **item = &handlers;
    while (*item != instance) { assert(*item != NULL); item = &(*item)->next; }
    assert(instance->base == base && instance->id == id);
    *item = instance->next;
    free(instance);
    assert(pthread_mutex_unlock(&events_lock) == 0);
    assert(pthread_mutex_lock(&lock) == 0);
    --status.live_handlers;
    assert(pthread_mutex_unlock(&lock) == 0);
    return ESP_OK;
}

esp_err_t esp_event_post(esp_event_base_t base, int32_t id, const void *data,
    size_t size, TickType_t timeout)
{
    (void)timeout;
    assert(size <= sizeof(((event_t *)0)->data));
    if (base != ETH_EVENT && base != IP_EVENT && base != WIFI_EVENT) {
        esp_err_t error = call(ETH_FAKE_FENCE);
        if (error != ESP_OK) { return error; }
    }
    event_t *event = calloc(1, sizeof(*event));
    assert(event != NULL);
    event->base = base; event->id = id;
    if (size != 0) { memcpy(event->data, data, size); }
    assert(pthread_mutex_lock(&events_lock) == 0);
    if (events_tail != NULL) { events_tail->next = event; }
    else { events_head = event; }
    events_tail = event;
    assert(pthread_mutex_unlock(&events_lock) == 0);
    return ESP_OK;
}

void fake_eth_flush(void)
{
    /* NOTE: 默认事件循环串行执行回调，unregister 等待回调完成。 */
    for (;;) {
        assert(pthread_mutex_lock(&events_lock) == 0);
        event_t *event = events_head;
        if (event == NULL) { pthread_mutex_unlock(&events_lock); return; }
        events_head = event->next;
        if (events_head == NULL) { events_tail = NULL; }
        struct fake_event_handler *handler = handlers;
        while (handler != NULL) {
            if (handler->base == event->base &&
                (handler->id == ESP_EVENT_ANY_ID || handler->id == event->id)) {
                handler->callback(handler->ctx, event->base, event->id, event->data);
            }
            handler = handler->next;
        }
        assert(pthread_mutex_unlock(&events_lock) == 0);
        free(event);
    }
}

static esp_err_t delete_mac(esp_eth_mac_t *mac)
{
    esp_err_t error = call(ETH_FAKE_MAC_DELETE);
    if (error != ESP_OK) { return error; }
    free(mac);
    assert(pthread_mutex_lock(&lock) == 0);
    --status.live_macs;
    assert(pthread_mutex_unlock(&lock) == 0);
    return ESP_OK;
}

static esp_err_t delete_phy(esp_eth_phy_t *phy)
{
    esp_err_t error = call(ETH_FAKE_PHY_DELETE);
    if (error != ESP_OK) { return error; }
    free(phy);
    assert(pthread_mutex_lock(&lock) == 0);
    --status.live_phys;
    assert(pthread_mutex_unlock(&lock) == 0);
    return ESP_OK;
}

gs232b_error_t gs232b_esp_w5500_create(gs232b_shared_spi_t *bus,
    esp_eth_mac_t **mac, esp_eth_phy_t **phy)
{
    if (call(ETH_FAKE_FACTORY) != ESP_OK) { return GS232B_ERROR_RESOURCE_EXHAUSTED; }
    assert(atomic_load(&bus->ethernet_enabled));
    *mac = calloc(1, sizeof(**mac)); *phy = calloc(1, sizeof(**phy));
    assert(*mac != NULL && *phy != NULL);
    (*mac)->del = delete_mac; (*phy)->del = delete_phy;
    factory_bus = bus;
    assert(pthread_mutex_lock(&lock) == 0);
    ++status.live_macs; ++status.live_phys;
    assert(pthread_mutex_unlock(&lock) == 0);
    return GS232B_OK;
}

esp_err_t esp_eth_driver_install(const esp_eth_config_t *config, esp_eth_handle_t *driver)
{
    esp_err_t error = call(ETH_FAKE_INSTALL);
    if (error != ESP_OK) { return error; }
    assert(config->mac != NULL && config->phy != NULL);
    uint8_t version;
    assert(gs232b_shared_spi_w5500_transfer(factory_bus, 0, 0x39, NULL, &version, 1) == GS232B_OK);
    *driver = calloc(1, sizeof(**driver));
    assert(*driver != NULL);
    (*driver)->references = 1; (*driver)->bus = factory_bus;
    assert(pthread_mutex_lock(&lock) == 0);
    ++status.live_drivers;
    assert(pthread_mutex_unlock(&lock) == 0);
    return ESP_OK;
}

esp_err_t esp_eth_driver_uninstall(esp_eth_handle_t driver)
{
    esp_err_t error = call(ETH_FAKE_UNINSTALL);
    if (error != ESP_OK) { return error; }
    assert(!driver->started && driver->references == 1);
    free(driver);
    assert(pthread_mutex_lock(&lock) == 0);
    --status.live_drivers;
    assert(pthread_mutex_unlock(&lock) == 0);
    return ESP_OK;
}

esp_err_t esp_eth_ioctl(esp_eth_handle_t driver, esp_eth_io_cmd_t cmd, void *data)
{
    assert(driver != NULL && cmd == ETH_CMD_S_MAC_ADDR);
    esp_err_t error = call(ETH_FAKE_SET_MAC);
    if (error == ESP_OK) {
        assert(pthread_mutex_lock(&lock) == 0);
        memcpy(status.installed_mac, data, 6);
        assert(pthread_mutex_unlock(&lock) == 0);
    }
    return error;
}

esp_err_t esp_eth_start(esp_eth_handle_t driver)
{
    /* NOTE: 模拟 IDF 部分启动失败：FSM 先切换，后续操作才返回错误。 */
    driver->started = true;
    esp_err_t error = call(ETH_FAKE_START);
    if (error != ESP_OK) { return error; }
    return esp_event_post(ETH_EVENT, ETHERNET_EVENT_START, &driver, sizeof(driver), 0);
}

esp_err_t esp_eth_stop(esp_eth_handle_t driver)
{
    esp_err_t error = call(ETH_FAKE_STOP);
    if (error != ESP_OK) { return error; }
    if (!driver->started) { return ESP_ERR_INVALID_STATE; }
    driver->started = false;
    return esp_event_post(ETH_EVENT, ETHERNET_EVENT_STOP, &driver, sizeof(driver), 0);
}

esp_netif_t *esp_netif_new(const esp_netif_config_t *config)
{
    if (call(ETH_FAKE_NETIF) != ESP_OK) { return NULL; }
    esp_netif_t *netif = calloc(1, sizeof(*netif));
    assert(netif != NULL && (config->stack == ESP_NETIF_NETSTACK_DEFAULT_ETH ||
        config->stack == ESP_NETIF_NETSTACK_DEFAULT_WIFI_STA));
    netif->flags = config->base->flags;
    assert(pthread_mutex_lock(&lock) == 0);
    if (config->stack == ESP_NETIF_NETSTACK_DEFAULT_ETH) {
        status.route_prio = config->base->route_prio;
    } else {
        assert(config->base->route_prio == GS232B_ESP_WIFI_STA_ROUTE_PRIORITY &&
            strcmp(config->base->if_key, "GS232B_STA") == 0);
    }
    ++status.live_netifs;
    assert(pthread_mutex_unlock(&lock) == 0);
    return netif;
}

void esp_netif_destroy(esp_netif_t *netif)
{
    assert(!netif->up);
    free(netif);
    assert(pthread_mutex_lock(&lock) == 0);
    --status.live_netifs;
    assert(pthread_mutex_unlock(&lock) == 0);
}

static void glue_event(void *ctx, esp_event_base_t base, int32_t id, void *data)
{
    (void)base;
    struct fake_eth_glue *glue = ctx;
    if (*(esp_eth_handle_t *)data != glue->driver || glue->netif == NULL) { return; }
    esp_netif_t *netif = glue->netif;
    if (id == ETHERNET_EVENT_CONNECTED) {
        netif->up = true;
        if ((netif->flags & ESP_NETIF_DHCP_CLIENT) && netif->dhcp == ESP_NETIF_DHCP_INIT) {
            (void)esp_netif_dhcpc_start(netif);
        }
    } else if (id == ETHERNET_EVENT_STOP) {
        esp_netif_action_stop(netif, base, id, data);
    } else if (id == ETHERNET_EVENT_DISCONNECTED) {
        netif->up = false;
        if (netif->dhcp == ESP_NETIF_DHCP_STARTED) {
            netif->dhcp = ESP_NETIF_DHCP_INIT;
            memset(&netif->ip, 0, sizeof(netif->ip));
        }
    }
}

esp_eth_netif_glue_handle_t esp_eth_new_netif_glue(esp_eth_handle_t driver)
{
    if (call(ETH_FAKE_GLUE) != ESP_OK) { return NULL; }
    esp_eth_netif_glue_handle_t glue = calloc(1, sizeof(*glue));
    assert(glue != NULL);
    glue->driver = driver;
    if (esp_event_handler_instance_register(ETH_EVENT, ESP_EVENT_ANY_ID, glue_event,
            glue, &glue->handler) != ESP_OK) { free(glue); return NULL; }
    ++driver->references;
    assert(pthread_mutex_lock(&lock) == 0);
    ++status.live_glues;
    assert(pthread_mutex_unlock(&lock) == 0);
    return glue;
}

esp_err_t esp_eth_del_netif_glue(esp_eth_netif_glue_handle_t glue)
{
    esp_err_t error = call(ETH_FAKE_GLUE_DELETE);
    if (error != ESP_OK) { return error; }
    error = esp_event_handler_instance_unregister(ETH_EVENT, ESP_EVENT_ANY_ID, glue->handler);
    if (error != ESP_OK) { return error; }
    --glue->driver->references;
    free(glue);
    assert(pthread_mutex_lock(&lock) == 0);
    --status.live_glues;
    assert(pthread_mutex_unlock(&lock) == 0);
    return ESP_OK;
}

esp_err_t esp_netif_attach(esp_netif_t *netif, void *ctx)
{
    esp_err_t error = call(ETH_FAKE_ATTACH);
    if (error == ESP_OK) { ((struct fake_eth_glue *)ctx)->netif = netif; }
    return error;
}

void esp_netif_action_stop(void *ctx, esp_event_base_t base, int32_t id, void *data)
{
    (void)base; (void)id; (void)data;
    esp_netif_t *netif = ctx;
    netif->up = false;
    if (netif->flags & ESP_NETIF_DHCP_CLIENT) {
        netif->dhcp = ESP_NETIF_DHCP_INIT;
        memset(&netif->ip, 0, sizeof(netif->ip));
    }
}

esp_err_t esp_netif_set_ip_info(esp_netif_t *netif, const esp_netif_ip_info_t *ip)
{
    esp_err_t error = call(ETH_FAKE_SET_IP);
    if (error != ESP_OK) { return error; }
    assert(netif->dhcp == ESP_NETIF_DHCP_STOPPED);
    netif->ip = *ip;
    return ESP_OK;
}

esp_err_t esp_netif_get_ip_info(esp_netif_t *netif, esp_netif_ip_info_t *ip)
{
    esp_err_t error = call(ETH_FAKE_GET_IP);
    if (error == ESP_OK) { *ip = netif->ip; }
    return error;
}

esp_err_t esp_netif_set_dns_info(esp_netif_t *netif, esp_netif_dns_type_t type,
    esp_netif_dns_info_t *dns)
{
    esp_err_t error = call(ETH_FAKE_SET_DNS);
    if (error != ESP_OK) { return error; }
    /* NOTE: 模拟 IDF 6.1 对零 DNS 的参数拒绝。 */
    if (dns->ip.u_addr.ip4.addr == 0) { return ESP_ERR_ESP_NETIF_INVALID_PARAMS; }
    assert(netif->dhcp == ESP_NETIF_DHCP_STOPPED);
    netif->dns[type] = *dns;
    return ESP_OK;
}

esp_err_t esp_netif_get_dns_info(esp_netif_t *netif, esp_netif_dns_type_t type,
    esp_netif_dns_info_t *dns)
{
    esp_err_t error = call(ETH_FAKE_GET_DNS);
    if (error == ESP_OK) { *dns = netif->dns[type]; }
    return error;
}

esp_err_t esp_netif_dhcpc_stop(esp_netif_t *netif)
{
    esp_err_t error = call(ETH_FAKE_DHCP_STOP);
    if (error != ESP_OK) { return error; }
    if (netif->dhcp == ESP_NETIF_DHCP_STOPPED) { return ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED; }
    netif->dhcp = ESP_NETIF_DHCP_STOPPED;
    return ESP_OK;
}

esp_err_t esp_netif_dhcpc_start(esp_netif_t *netif)
{
    esp_err_t error = call(ETH_FAKE_DHCP_START);
    if (error != ESP_OK) { return error; }
    assert(netif->ip.ip.addr == 0 && netif->ip.netmask.addr == 0 && netif->ip.gw.addr == 0);
    if (!netif->up) {
        assert(netif->dns[0].ip.u_addr.ip4.addr == 0 && netif->dns[1].ip.u_addr.ip4.addr == 0);
    }
    memset(netif->dns, 0, sizeof(netif->dns));
    netif->dhcp = netif->up ? ESP_NETIF_DHCP_STARTED : ESP_NETIF_DHCP_INIT;
    return ESP_OK;
}

esp_err_t esp_netif_dhcpc_get_status(esp_netif_t *netif, esp_netif_dhcp_status_t *dhcp)
{
    *dhcp = netif->dhcp;
    return ESP_OK;
}

void fake_eth_link(gs232b_esp_ethernet_t *adapter, bool up)
{
    esp_eth_handle_t driver = adapter->driver;
    assert(esp_event_post(ETH_EVENT, up ? ETHERNET_EVENT_CONNECTED : ETHERNET_EVENT_DISCONNECTED,
        &driver, sizeof(driver), 0) == ESP_OK);
    fake_eth_flush();
}

void fake_eth_lease(gs232b_esp_ethernet_t *adapter, gs232b_ethernet_ipv4_t ip)
{
    assert(adapter->netif->dhcp == ESP_NETIF_DHCP_STARTED);
    adapter->netif->ip = (esp_netif_ip_info_t){
        .ip.addr = esp_netif_htonl(ip.address), .netmask.addr = esp_netif_htonl(ip.netmask),
        .gw.addr = esp_netif_htonl(ip.gateway)
    };
    for (unsigned i = 0; i < 2; ++i) {
        adapter->netif->dns[i].ip.u_addr.ip4.addr = esp_netif_htonl(ip.dns[i]);
    }
    const ip_event_got_ip_t event = {adapter->netif, adapter->netif->ip, true};
    assert(esp_event_post(IP_EVENT, IP_EVENT_ETH_GOT_IP, &event, sizeof(event), 0) == ESP_OK);
    fake_eth_flush();
}

void fake_eth_lost_ip(gs232b_esp_ethernet_t *adapter)
{
    memset(&adapter->netif->ip, 0, sizeof(adapter->netif->ip));
    const ip_event_got_ip_t event = {.esp_netif = adapter->netif};
    assert(esp_event_post(IP_EVENT, IP_EVENT_ETH_LOST_IP, &event, sizeof(event), 0) == ESP_OK);
    fake_eth_flush();
}

esp_netif_dhcp_status_t fake_eth_dhcp(gs232b_esp_ethernet_t *adapter)
{
    return adapter->netif->dhcp;
}

gs232b_ethernet_ipv4_t fake_eth_runtime(gs232b_esp_ethernet_t *adapter)
{
    const esp_netif_t *netif = adapter->netif;
    return (gs232b_ethernet_ipv4_t){
        .address = esp_netif_htonl(netif->ip.ip.addr),
        .netmask = esp_netif_htonl(netif->ip.netmask.addr),
        .gateway = esp_netif_htonl(netif->ip.gw.addr),
        .dns = {esp_netif_htonl(netif->dns[0].ip.u_addr.ip4.addr),
                esp_netif_htonl(netif->dns[1].ip.u_addr.ip4.addr)}
    };
}

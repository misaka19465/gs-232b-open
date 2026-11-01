#ifndef GS232B_PERSISTENCE_H
#define GS232B_PERSISTENCE_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#include "gs232b/coordinates.h"
#include "gs232b/ethernet.h"
#include "gs232b/ports.h"

#define GS232B_PERSISTENCE_RECORD_FORMAT_VERSION 1U
#define GS232B_PERSISTENCE_MAX_PAYLOAD_SIZE 512U
#define GS232B_PERSISTENCE_RECORD_HEADER_SIZE 18U
#define GS232B_PERSISTENCE_RECORD_CHECKSUM_SIZE 4U
#define GS232B_PERSISTENCE_MAX_RECORD_SIZE \
    (GS232B_PERSISTENCE_RECORD_HEADER_SIZE + GS232B_PERSISTENCE_MAX_PAYLOAD_SIZE + \
     GS232B_PERSISTENCE_RECORD_CHECKSUM_SIZE)
#define GS232B_PERSISTENCE_SCHEMA_DISPLAY 1U
#define GS232B_PERSISTENCE_SCHEMA_NETWORK 2U
#define GS232B_PERSISTENCE_SCHEMA_CONFIG_META 1U
#define GS232B_NETWORK_SSID_MAX 32U
#define GS232B_NETWORK_PASSWORD_MAX 64U

typedef uint64_t gs232b_revision_t;

/* NOTE: 序列化布局为 magic(4)、format(1)、domain(1)、schema(2)、revision(8)、
 * payload_size(2)、payload 和 CRC-32(4)，所有整数均为 little-endian。 */

typedef enum {
    GS232B_NETWORK_SECURITY_OPEN = 0,
    GS232B_NETWORK_SECURITY_WPA2_PERSONAL = 1
} gs232b_network_security_t;

typedef struct {
    gs232b_ethernet_config_t eth;
    bool enabled;
    gs232b_network_security_t security;
    uint8_t ssid_length;
    char ssid[GS232B_NETWORK_SSID_MAX + 1U];
    uint8_t password_length;
    char password[GS232B_NETWORK_PASSWORD_MAX + 1U];
    uint32_t last_confirmed_trial_id;
} gs232b_network_config_t;

typedef struct {
    gs232b_store_domain_t domain;
    uint16_t schema_version;
    uint64_t revision;
    uint16_t payload_size;
    uint8_t payload[GS232B_PERSISTENCE_MAX_PAYLOAD_SIZE];
} gs232b_persisted_record_t;

/**
 * @brief 加载并校验指定域的最新完整记录。
 * @param store 原子域存储端口。
 * @param domain 逻辑配置域。
 * @param record 输出记录；失败时清零。
 * @return 不存在、损坏或存储失败时返回对应错误。
 */
gs232b_error_t gs232b_persistence_load(const gs232b_store_t *store,
                                      gs232b_store_domain_t domain,
                                      gs232b_persisted_record_t *record);

/**
 * @brief 加载指定 schema；当前槽版本不支持时回退上一完整槽。
 */
gs232b_error_t gs232b_persistence_load_schema(const gs232b_store_t *store,
                                             gs232b_store_domain_t domain,
                                             uint16_t schema_version,
                                             gs232b_persisted_record_t *record);

/**
 * @brief 直接读取上一完整槽，供域语义校验失败时恢复。
 */
gs232b_error_t gs232b_persistence_load_previous(
    const gs232b_store_t *store,
    gs232b_store_domain_t domain,
    gs232b_persisted_record_t *record);

/**
 * @brief 以 expected revision 比较并提交一个新版本。
 * @param store 原子域存储端口。
 * @param domain 逻辑配置域。
 * @param expected_revision 当前调用方已读取的 revision；初次创建时为 0。
 * @param schema_version 域 payload 的非零 schema 版本。
 * @param payload 序列化后的域 payload。
 * @param payload_size payload 字节数。
 * @param new_revision 成功提交后的 revision；失败时置零。
 * @return revision 冲突、校验或存储失败时返回对应错误。
 */
gs232b_error_t gs232b_persistence_commit(const gs232b_store_t *store,
                                         gs232b_store_domain_t domain,
                                         uint64_t expected_revision,
                                         uint16_t schema_version,
                                         const uint8_t *payload,
                                         size_t payload_size,
                                         uint64_t *new_revision);

/**
 * @brief 加载并验证显示模式设置。
 * @param store 原子域存储端口。
 * @param settings 输出显示模式；错误时使用默认值。
 * @param revision 输出记录 revision；错误时置零。
 */
gs232b_error_t gs232b_persistence_display_load(
    const gs232b_store_t *store,
    gs232b_azimuth_display_settings_t *settings,
    uint64_t *revision);

/**
 * @brief 以 revision 比较并保存显示模式设置。
 * @param store 原子域存储端口。
 * @param expected_revision 当前显示域 revision；无记录时为零。
 * @param settings 待保存设置。
 * @param new_revision 成功提交后的 revision；失败时置零。
 */
gs232b_error_t gs232b_persistence_display_commit(
    const gs232b_store_t *store,
    uint64_t expected_revision,
    const gs232b_azimuth_display_settings_t *settings,
    uint64_t *new_revision);

/**
 * @brief 加载并验证网络域配置；错误时清零输出，不触碰其他域。
 */
gs232b_error_t gs232b_persistence_network_load(
    const gs232b_store_t *store,
    gs232b_network_config_t *config,
    uint64_t *revision);

/**
 * @brief 以 revision 比较并保存网络域配置。
 */
gs232b_error_t gs232b_persistence_network_commit(
    const gs232b_store_t *store,
    uint64_t expected_revision,
    const gs232b_network_config_t *config,
    uint64_t *new_revision);

/** @brief 读取跨重启单调递增的网络 trial ID 分配器；无记录时返回零。 */
gs232b_error_t gs232b_persistence_trial_id_load(const gs232b_store_t *store,
    uint32_t *last_trial_id);
/** @brief 以 expected ID 比较并持久化下一个网络 trial ID。 */
gs232b_error_t gs232b_persistence_trial_id_commit(const gs232b_store_t *store,
    uint32_t expected_last_trial_id, uint32_t next_trial_id);

#endif

#ifndef GS232B_SHARED_SPI_H
#define GS232B_SHARED_SPI_H

#include <stdatomic.h>

#include "gs232b/output_manager.h"

#define GS232B_SHARED_SPI_MAX_TRANSFER 16384U

/* NOTE: 平台端口私有于本适配器。lock/unlock 是两个 device 共用的入口锁，
 * 覆盖完整同步事务；595 另以硬件 host acquire/release 覆盖锁存。
 * transmit 回调即使失败也须在硬件停止、缓冲区不再被引用后返回。
 * set_latch 每次调用需满足 STCP 的建立/保持及高低脉宽，失败可部分生效。
 * acquire_595 失败时不得保留 host；release_595/unlock 不得失败。 */
typedef struct {
    void *ctx;
    gs232b_error_t (*lock)(void *ctx);
    void (*unlock)(void *ctx);
    gs232b_error_t (*acquire_595)(void *ctx);
    gs232b_error_t (*transmit_595)(void *ctx, uint8_t byte);
    gs232b_error_t (*set_latch)(void *ctx, bool high);
    void (*release_595)(void *ctx);
    gs232b_error_t (*transmit_w5500)(void *ctx, uint8_t control, uint16_t address,
                                    const void *tx, void *rx, size_t size);
    gs232b_error_t (*set_pwm)(void *ctx, uint16_t permille);
} gs232b_shared_spi_io_t;

/* NOTE: 布局仅用于静态分配，不得复制活动实例或直接访问 ethernet_enabled。
 * 输出仍由唯一输出管理器串行调用；网络事务可以来自其他任务。
 * 门控仅由一个生命周期所有者串行调用，不能并发开关或绕过复位等待开门。
 * 初始化/销毁须在所有调用之外，端口及 ctx 借用至所有使用者停用。 */
typedef struct {
    gs232b_shared_spi_io_t io;
    atomic_bool ethernet_enabled;
} gs232b_shared_spi_t;

/** @brief 初始化共享总线适配器，初始禁止 W5500 访问；不提交物理输出。 */
gs232b_error_t gs232b_shared_spi_init(gs232b_shared_spi_t *bus,
                                      gs232b_shared_spi_io_t io);

/** @brief 返回仅供输出管理器使用的完整 595/PWM 提交端口。 */
gs232b_output_driver_t gs232b_shared_spi_output_driver(gs232b_shared_spi_t *bus);

/**
 * @brief 在同一入口锁内开关 W5500 访问，关闭成功即已排空在途同步事务。
 * @note 关闭先禁止新访问，再等待在途事务；超时仍保持禁止新访问，但尚未排空。
 *       只允许复位协调器及网络生命周期所有者开门；初始化和复位等待时关闭。
 */
gs232b_error_t gs232b_shared_spi_set_ethernet_enabled(gs232b_shared_spi_t *bus,
                                                     bool enabled);

/**
 * @brief 执行一次完整 W5500 VDM 事务，control 为包含读写位的 8bit 控制字段。
 * @param tx 写事务数据；读事务传 NULL。不得同时提供 tx 和 rx。
 * @param rx 读事务缓冲区；写事务传 NULL。失败时内容不可用。
 * @param size 1..GS232B_SHARED_SPI_MAX_TRANSFER，缓冲区仅借用至返回。
 * @return 门关闭返回 NOT_READY；入口锁超时原样返回，不访问硬件或锁存脚。
 */
gs232b_error_t gs232b_shared_spi_w5500_transfer(gs232b_shared_spi_t *bus,
                                               uint8_t control, uint16_t address,
                                               const void *tx, void *rx, size_t size);

#endif

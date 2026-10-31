#ifndef GS232B_ESP_LEDC_H
#define GS232B_ESP_LEDC_H

#include "gs232b/types.h"

#ifdef ESP_PLATFORM

#define GS232B_BOARD_SPEED_PWM_GPIO 20
#define GS232B_LEDC_FREQUENCY_HZ 20000U
#define GS232B_LEDC_RESOLUTION_BITS 10U
#define GS232B_LEDC_FULL_DUTY (UINT32_C(1) << GS232B_LEDC_RESOLUTION_BITS)

/* NOTE: 私有布局仅供静态分配。GPIO20、低速 TIMER0/CHANNEL0 只建立一个实例；
 * set API 只供共享 SPI 输出驱动使用，须与唯一输出管理器共用串行所有者。
 * 不得直接调用 IDF LEDC、GPIO 或 UART0 API 争用该引脚，也不得复制活动实例。 */
typedef struct {
    bool timer_open;
    bool channel_open;
    bool opened;
} gs232b_esp_ledc_t;

/**
 * @brief 初始化 GPIO20 的 20kHz/10bit LEDC，初始占空比为零。
 * @param adapter 清零且未活动的实例；失败后尝试释放已取得资源。
 * @note 清理失败时须先成功 close 再 open；不创建任务、不初始化 UART0。
 */
gs232b_error_t gs232b_esp_ledc_open(gs232b_esp_ledc_t *adapter);

/**
 * @brief 将 0..1000‰ 请求四舍五入换算成 0..1024 计数，不访问硬件。
 * @return 参数非法时返回 INVALID_ARGUMENT，非空 duty 输出置零。
 * @note C3 的最大分辨率为 14bit；本模块固定 10bit，IDF 允许满占空比 1024。
 */
gs232b_error_t gs232b_esp_ledc_duty_from_permille(uint16_t permille, uint32_t *duty);

/**
 * @brief 提交 0..1024 计数并更新通道；非法计数不访问 LEDC。
 * @return 未打开返回 NOT_READY；任一 IDF 写入失败原样映射，不自动重试。
 * @note OK 仅表示驱动接纳，生效从下个 PWM 周期开始；无 SPEED 电压读回，
 *       不能证明滤波/运放输出或电机转速达到请求值。失败可能部分生效。
 */
gs232b_error_t gs232b_esp_ledc_set_duty(gs232b_esp_ledc_t *adapter, uint32_t duty);

/** @brief 提交 0..1000‰ PWM 请求；使用相同的边界、错误及提交语义。 */
gs232b_error_t gs232b_esp_ledc_set_permille(gs232b_esp_ledc_t *adapter,
                                          uint16_t permille);

/**
 * @brief 先输出低电平再释放引脚、通道和定时器；允许重复关闭或重试释放。
 * @note 调用前须停止并发访问且由输出管理器成功全停；本函数不清方向位。
 */
gs232b_error_t gs232b_esp_ledc_close(gs232b_esp_ledc_t *adapter);

#endif
#endif

#ifndef GS232B_ESP_ADC_H
#define GS232B_ESP_ADC_H

#include "gs232b/ports.h"

#ifdef ESP_PLATFORM
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_oneshot.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

/* NOTE: 私有布局仅供静态分配；不得复制活动实例或直接访问驱动/队列。
 * ADC1 只建立一个实例。采集端单所有者，读取端可与采集端并发。 */
typedef struct {
    adc_oneshot_unit_handle_t unit;
    adc_cali_handle_t calibration[GS232B_AXIS_COUNT];
    QueueHandle_t latest[GS232B_AXIS_COUNT];
    bool opened;
} gs232b_esp_adc_t;

/**
 * @brief 配置 C3 的 ADC1_CH1/AZ、ADC1_CH0/EL，12bit、12dB 及 eFuse 曲线校准。
 * @param adapter 清零且未活动的实例；生命周期覆盖所有借用的 sampler 端口。
 * @return 校准不可用时返回 NOT_READY，绝不退回默认参考电压或比例换算。
 * @note 仅分配和配置，不创建任务或立即采样。失败时尝试释放已取得资源；
 *       释放失败后须先成功 close 再重试 open。启动顺序由应用保证输出先全停。
 */
gs232b_error_t gs232b_esp_adc_open(gs232b_esp_adc_t *adapter);

/**
 * @brief 在采集任务中各读取一次 AZ/EL，并以零等待覆盖每轴最新采样邮箱。
 * @return 任一轴读取或校准失败时返回 NOT_READY，同时投递该轴无效样本；
 *         另一轴继续独立采集。未打开时不访问硬件。
 * @note oneshot 读取是同步的，不可在 ISR、运动循环或 sampler.read 中调用。
 *       由单独采集任务定期调用（建议每 5ms），调度不补发历史样本。
 */
gs232b_error_t gs232b_esp_adc_capture(gs232b_esp_adc_t *adapter);

/**
 * @brief 借用只读、零等待的最新采样端口；不会触发硬件读取或重新校准。
 * @return 无样本、采集失败、未来时间或超过 100ms 时 read 返回 NOT_READY，
 *         并清空整个输出样本；成功保留实际获取时间，电压单位为微伏。
 * @note 电压为 ADC 引脚电压；分压、零点、角度换算及滤波由反馈/运动核心处理。
 */
gs232b_sampler_t gs232b_esp_adc_sampler(gs232b_esp_adc_t *adapter);

/**
 * @brief 使采样失效并释放队列、校准和 ADC1；允许重复关闭或重试失败的释放。
 * @note open/close 必须在所有 capture/read 调用结束后执行，不能与它们并发。
 *       关闭后借用的 sampler 返回 NOT_READY；重开后须取得新样本才可用。
 */
gs232b_error_t gs232b_esp_adc_close(gs232b_esp_adc_t *adapter);

#endif
#endif

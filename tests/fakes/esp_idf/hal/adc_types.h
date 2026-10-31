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

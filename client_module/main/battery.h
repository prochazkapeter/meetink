#ifndef BATTERY_H
#define BATTERY_H

#ifdef __cplusplus
extern "C"
{
#endif

#define ADC_EXAMPLE_CALI_SCHEME ESP_ADC_CAL_VAL_EFUSE_VREF
#define EXAMPLE_ADC_ATTEN ADC_ATTEN_DB_12
#define ADC1_EXAMPLE_CHAN0 ADC_CHANNEL_6
#define DIV_RATIO (1.0f + 1.3f) / 1.3f

    void adc_init(void);
    void adc_deinit(void);
    float measure_batt_voltage(void);

#ifdef __cplusplus
}
#endif

#endif
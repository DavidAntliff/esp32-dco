/*
 * MIT License
 *
 * Copyright (c) 2018 David Antliff
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/ledc.h"
#include "esp_log.h"

#define TAG "dco"

#define GPIO_LED       (CONFIG_ONBOARD_LED_GPIO)
#define GPIO_DCO_OUT   (CONFIG_DCO_OUTPUT_GPIO)

//#define FREQUENCY 1.23456789  // Hz
//#define FREQUENCY 12.3456789  // Hz
//#define FREQUENCY 123.456789  // Hz
//#define FREQUENCY 1234.56789  // Hz
#define FREQUENCY 12345.6789  // Hz
//#define FREQUENCY 123456.789  // Hz
//#define FREQUENCY 1234567.89  // Hz
//#define FREQUENCY 12345678.9  // Hz

#define DUTY_CYCLE  0.50

#define PRECISION   LEDC_TIMER_10_BIT
#define TIMER_MODE  LEDC_HIGH_SPEED_MODE
//#define TIMER_MODE  LEDC_LOW_SPEED_MODE

void dco_led_pwm_task(void * pvParameter)
{
    ledc_timer_config_t ledc_timer =
    {
//        .duty_resolution = PRECISION,     ESP-IDF v3.0+
        .bit_num = PRECISION,
        .freq_hz = FREQUENCY,
        .speed_mode = TIMER_MODE,
        .timer_num = LEDC_TIMER_0,
    };
    ledc_timer_config(&ledc_timer);

    ledc_channel_config_t ledc_channel =
    {
        .channel = LEDC_CHANNEL_0,
        .duty = 0,
        .gpio_num = GPIO_DCO_OUT,
        .speed_mode = TIMER_MODE,
        .timer_sel = LEDC_TIMER_0,
    };
    ledc_channel_config(&ledc_channel);

    ledc_set_duty(ledc_channel.speed_mode, ledc_channel.channel, (1 << PRECISION) * DUTY_CYCLE);

    // also drive onboard LED
    gpio_pad_select_gpio(GPIO_LED);
    gpio_set_direction(GPIO_LED, GPIO_MODE_OUTPUT);
    gpio_matrix_out(GPIO_LED, LEDC_HS_SIG_OUT0_IDX, 0, 0);

    ESP_LOGI(TAG, "Frequency %.3f Hz, duty %.1f%%", FREQUENCY, DUTY_CYCLE * 100.0f);

    while (1)
    {
        vTaskDelay(1);
    }

    vTaskDelete(NULL);
}

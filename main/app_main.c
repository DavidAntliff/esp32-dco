#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "driver/gpio.h"
#include "driver/rmt.h"
#include "esp_log.h"

#include "esp32-utils/system.h"

#define TAG "dco"

#define BLUE_LED_GPIO (GPIO_NUM_2)
#define DCO_OUT_GPIO (GPIO_NUM_4)

#define RMT_TX_CHANNEL RMT_CHANNEL_0
#define RMT_CLK_DIV    80   // 1 MHz resolution

#define FREQUENCY 10.0   // Hz

void dco_task(void * pvParameter)
{
    gpio_pad_select_gpio(DCO_OUT_GPIO);
    gpio_set_direction(DCO_OUT_GPIO, GPIO_MODE_OUTPUT);

    float period = 1.0 / FREQUENCY;
    TickType_t delay = 1000 * period / portTICK_RATE_MS / 2;
    ESP_LOGI(TAG, "Delay %d ticks", delay);

    while(1)
    {
        ESP_LOGI(TAG, "loop");
//        gpio_set_level(DCO_OUT_GPIO, 0);
        vTaskDelay(delay);
//        gpio_set_level(DCO_OUT_GPIO, 1);
        vTaskDelay(delay);
    }
}

void dco_rmt_task(void * pvParameter)
{
    // calculate the period from the requested frequency
    double period = 1.0 / FREQUENCY;

    // fetch the APB clock rate
    uint32_t apb_freq = get_apb_frequency_MHz() * 1000000;
    ESP_LOGI(TAG, "apb freq %d", apb_freq);

    // half the period and determine the equivalent number of APB clock periods

    // we have 63 items available, with 15 bits per item allowing for 32767 periods.
    // This gives a maximum of 32767 * 63 = 2,064,321 periods per cycle.
    // This corresponds to a minimum frequency of approximately 38.75 Hz
    // For slower rates, the clock divider can be used. E.g. a divider of 4 will
    // correspond to a minimum frequency of approximately 9.688 Hz.

    // So find the minimum clock divider that allows for an approximation
    // to the desired frequency.

//    // calculate optimal way to produce requested frequency
//    double reqd_period = 1.0 / FREQUENCY;
//
//    ESP_LOGI(TAG, "reqd frequency %g Hz", FREQUENCY);
//    ESP_LOGI(TAG, "reqd period %g s", reqd_period);

    // for now, generate 500 kHz signal
    uint8_t clock_divider = 80;
    uint32_t high_count = 1;
    uint32_t low_count = 1;

    rmt_config_t rmt_tx = {
        .rmt_mode = RMT_MODE_TX,
        .channel = RMT_TX_CHANNEL,
        .gpio_num = DCO_OUT_GPIO,
        .mem_block_num = 1,  // single block
        .clk_div = clock_divider,
        .tx_config.loop_en = true,
        .tx_config.carrier_en = false,
        .tx_config.idle_level = RMT_IDLE_LEVEL_LOW,
        .tx_config.idle_output_en = true,
    };
    rmt_config(&rmt_tx);
    rmt_driver_install(rmt_tx.channel, 0, 0);

    size_t num_items = 4;
    rmt_item32_t * items = calloc(num_items, sizeof(*items));
    for (size_t i = 0; i < num_items; ++i)
    {
        items[i].level0 = 1;
        items[i].duration0 = high_count;
        items[i].level1 = 0;
        items[i].duration1 = low_count;
    }

    // shorten final item to accommodate final idle period
    --items[num_items - 1].duration1;

    rmt_set_tx_loop_mode(RMT_TX_CHANNEL, true);
    rmt_write_items(RMT_TX_CHANNEL, items, num_items, false);

    while(1)
        vTaskDelay(1);

    vTaskDelete(NULL);
}

void rmt_task(void * pvParameter)
{
    rmt_config_t rmt_tx = {
        .rmt_mode = RMT_MODE_TX,
        .channel = RMT_CHANNEL_0,
        .gpio_num = GPIO_NUM_4,
        .mem_block_num = 1,
        .clk_div = 80,
        .tx_config.loop_en = true,
        .tx_config.carrier_en = false,
        .tx_config.idle_level = RMT_IDLE_LEVEL_LOW,
        .tx_config.idle_output_en = true,
    };
    rmt_config(&rmt_tx);
    rmt_driver_install(rmt_tx.channel, 0, 0);

    size_t num_items = 4;
    rmt_item32_t * items = calloc(num_items, sizeof(*items));
    for (size_t i = 0; i < num_items; ++i)
    {
        items[i].level0 = 1;
        items[i].duration0 = 1;
        items[i].level1 = 0;
        items[i].duration1 = 1;
    }

    // shorten final item to accommodate final idle period
    --items[num_items - 1].duration1;

    rmt_write_items(RMT_CHANNEL_0, items, num_items, false);

    while(1)
    {
        ESP_LOGI(TAG, "wait");
        vTaskDelay(10);
    }
}


void app_main()
{
    xTaskCreate(&dco_rmt_task, "dco_task", 2048, NULL, 5, NULL);
//    xTaskCreate(&dco_task, "dco_task", 2048, NULL, 5, NULL);
//    xTaskCreate(&rmt_task, "rmt_task", 2048, NULL, 5, NULL);
}


#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "driver/gpio.h"
#include "driver/rmt.h"
#include "esp_log.h"

#include "esp32-utils/system.h"
#include "esp32-utils/rmt.h"

#define TAG "dco"

#define BLUE_LED_GPIO (GPIO_NUM_2)
#define DCO_OUT_GPIO (GPIO_NUM_4)

#define RMT_TX_CHANNEL RMT_CHANNEL_0

#define FREQUENCY 51.0   // Hz
//#define FREQUENCY 4998.0   // Hz
//#define FREQUENCY 500000.0   // Hz
//#define FREQUENCY 163700.0   // Hz
//#define FREQUENCY 1637000.0   // Hz
//#define FREQUENCY 16300000.0   // Hz

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
    double frequency = FREQUENCY;
    ESP_LOGI(TAG, "frequency %f Hz", frequency);

    // calculate the period from the requested frequency
    double period = 1.0 / frequency;
    ESP_LOGI(TAG, "period %0.6f s", period);

    // fetch the APB clock rate
    uint32_t apb_freq = get_apb_frequency_MHz() * 1000000;
    ESP_LOGI(TAG, "apb freq %d", apb_freq);

    // lower divider provides higher resolution
    uint8_t clock_divider = 1;
    ESP_LOGI(TAG, "clock divider %d", clock_divider);

    // divided frequency
    double div_freq = apb_freq / clock_divider;
    ESP_LOGI(TAG, "div freq %f", div_freq);

    // maximum frequency
    double max_frequency = div_freq / 2.0;  // two transitions per cycle
    ESP_LOGI(TAG, "max frequency %f", max_frequency);
    assert(frequency <= max_frequency);

    // Determine the equivalent number of APB clock periods
    // Round to nearest int
    uint32_t period_count = (uint32_t)(period / (1.0 / div_freq) + 0.5);
    ESP_LOGI(TAG, "period_count %d", period_count);

    // Let's use a single RAM block for maximum interoperability with other software.
    // For looping, we have 63 items available, with 2 * 15 bits per item allowing for
    // 2 * 32767 periods (zero is reserved) = 65534 period counts per item.
    // This gives a maximum of 2 * 32767 * 63 = 4,128,642 periods per cycle.
    // For an 80MHz APB clock, this corresponds to a minimum frequency of approximately
    // 80,000,000 / 4,128,642 = 19.38 Hz
    // For slower rates, the clock divider can be used. E.g. a divider of 4 will
    // correspond to a minimum frequency of approximately 4.844 Hz.

    uint32_t high_count = period_count / 2;
    uint32_t low_count = period_count  - high_count;
    ESP_LOGI(TAG, "high_count %d", high_count);
    ESP_LOGI(TAG, "low_count %d", low_count);

    // So find the minimum clock divider that allows for an approximation
    // to the desired frequency.
    double actual_frequency = div_freq / period_count;
    ESP_LOGI(TAG, "actual frequency %f", actual_frequency);
    ESP_LOGI(TAG, "discrepancy %f ppm", (actual_frequency - frequency) / frequency * 1000000);

//    // calculate optimal way to produce requested frequency
//    double reqd_period = 1.0 / FREQUENCY;
//
//    ESP_LOGI(TAG, "reqd frequency %g Hz", FREQUENCY);
//    ESP_LOGI(TAG, "reqd period %g s", reqd_period);

    // For some reason, there needs to be some space at the end of the block
    //  - sometimes one item, sometimes two items, sometimes even three...
    const uint8_t max_items = RMT_MEM_ITEM_NUM - 3;

    // Items are split into two sections: 0 and 1

    // When looping, do not use the last item in the last mem block, as it is needed by the driver to implement the loop
    const uint32_t max_count_per_section = ((1 << RMT_ITEM_DURATION_WIDTH) - 1);
    const uint32_t max_counts_per_mem_block = 2 * max_count_per_section * max_items;
    ESP_LOGI(TAG, "max_counts_per_mem_block %d", max_counts_per_mem_block);

    // floor:
    const uint8_t max_periods_per_mem_block = max_counts_per_mem_block / period_count;
    ESP_LOGI(TAG, "max_periods_per_mem_block %d", max_periods_per_mem_block);

    // to reduce interrupt load at high frequencies, use multiple cycles
    //const uint8_t num_items =

    // each section of an item can count to 32767, so calculate number of sections for both high and low states
    uint8_t num_sections_high = high_count / max_count_per_section;
    if (num_sections_high * max_count_per_section != high_count)
    {
        ++num_sections_high;  // add 1 to include partial section
    }

    uint8_t num_sections_low = low_count / max_count_per_section;
    if (num_sections_low * max_count_per_section != low_count)
    {
        ++num_sections_low;  // add 1 to include partial section
    }

    ESP_LOGI(TAG, "num_sections_high %d", num_sections_high);
    ESP_LOGI(TAG, "num_sections_low %d", num_sections_low);

    const uint8_t sections_per_cycle = num_sections_high + num_sections_low;
    ESP_LOGI(TAG, "sections_per_cycle %d", sections_per_cycle);

    const uint8_t cycles_per_mem_block = (2 /*sections*/ * max_items) / sections_per_cycle;
    ESP_LOGI(TAG, "cycles_per_mem_block %d", cycles_per_mem_block);

    // round up
    size_t num_items = (cycles_per_mem_block * sections_per_cycle + 1) / 2;
    ESP_LOGI(TAG, "num_items %d", num_items);

    rmt_item32_t * items = calloc(num_items, sizeof(*items));
    size_t section_index = 0;
    for (size_t cycle = 0; cycle < cycles_per_mem_block; ++cycle)
    {
        // high sections
        uint32_t high_count_remaining = high_count;
        for (size_t i = 0; i < num_sections_high; ++i)
        {
            uint32_t duration = high_count_remaining < max_count_per_section ? high_count_remaining : max_count_per_section;
            high_count_remaining -= duration;
            size_t item_index = section_index / 2;
            if (duration > 0)
            {
                if (section_index % 2 == 0)
                {
                    items[item_index].level0 = 1;
                    items[item_index].duration0 = duration;
                }
                else
                {
                    items[item_index].level1 = 1;
                    items[item_index].duration1 = duration;
                }
                ++section_index;
            }
        }

        // low sections
        uint32_t low_count_remaining = low_count;
        for (size_t i = 0; i < num_sections_low; ++i)
        {
            uint32_t duration = low_count_remaining < max_count_per_section ? low_count_remaining : max_count_per_section;
            low_count_remaining -= duration;
            size_t item_index = section_index / 2;
            if (duration > 0)
            {
                if (section_index % 2 == 0)
                {
                    items[item_index].level0 = 0;
                    items[item_index].duration0 = duration;
                }
                else
                {
                    items[item_index].level1 = 0;
                    items[item_index].duration1 = duration;
                }
                ++section_index;
            }
        }
    }

    // shorten final item to accommodate final idle period
    if (section_index % 2 == 0)
    {
        --items[num_items - 1].duration1;
    }
    else
    {
        --items[num_items - 1].duration0;
    }

    for (size_t i = 0; i < num_items; ++i)
    {
        ESP_LOGI(TAG, "%2d: level0 %d, duration0 %5d | level1 %d, duration1 %5d", i, items[i].level0, items[i].duration0, items[i].level1, items[i].duration1);
    }

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

    rmt_write_items(RMT_TX_CHANNEL, items, num_items, false);

    while(1)
        vTaskDelay(1);

    vTaskDelete(NULL);
}

void rmt_task(void * pvParameter)
{
//    uint8_t clock_divider = 40;
//    uint32_t high_count = 1;
//    uint32_t low_count = 1;
    uint8_t clock_divider = 80;
    uint32_t high_count = 1;
    uint32_t low_count = 1;

    rmt_config_t rmt_tx = {
        .rmt_mode = RMT_MODE_TX,
        .channel = RMT_CHANNEL_0,
        .gpio_num = GPIO_NUM_4,
        .mem_block_num = 1,
        .clk_div = clock_divider,
        .tx_config.loop_en = true,
        .tx_config.carrier_en = false,
        .tx_config.idle_level = RMT_IDLE_LEVEL_LOW,
        .tx_config.idle_output_en = true,
    };
    rmt_config(&rmt_tx);
    rmt_driver_install(rmt_tx.channel, 0, 0);

    size_t num_items = 63;
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

    for (size_t i = 0; i < num_items; ++i)
    {
        ESP_LOGI(TAG, "%2d: level0 %d, duration0 %5d | level1 %d, duration1 %5d", i, items[i].level0, items[i].duration0, items[i].level1, items[i].duration1);
    }

    rmt_write_items(rmt_tx.channel, items, num_items, false);

    while(1)
    {
        //ESP_LOGI(TAG, "wait");
        vTaskDelay(1);
    }
}


void app_main()
{
    xTaskCreate(&dco_rmt_task, "dco_task", 2048, NULL, 5, NULL);
//    xTaskCreate(&dco_task, "dco_task", 2048, NULL, 5, NULL);
//    xTaskCreate(&rmt_task, "rmt_task", 2048, NULL, 5, NULL);
}


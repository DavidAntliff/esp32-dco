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

#include <stdio.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "driver/gpio.h"
#include "driver/rmt.h"
#include "esp_log.h"

#include "esp32-utils/system.h"
#include "esp32-utils/rmt.h"

#define TAG "dco"

#define GPIO_LED       (CONFIG_ONBOARD_LED_GPIO)
#define GPIO_DCO_OUT   (CONFIG_DCO_OUTPUT_GPIO)

#define RMT_TX_CHANNEL RMT_CHANNEL_0

// seems to need to be 61 or less for stability.
#define MAX_ITEMS_PER_BLOCK  (RMT_MEM_ITEM_NUM - 3)

// Be aware of the clock divider as it determines the minimum frequency that can be generated.
// Minimum generated frequency is determined by RMT clock divider:
//
//   min_freq = 80MHz / CLOCK_DIVIDER / (2 * 32767 * MAX_ITEMS_PER_BLOCK)  Hz
//
// However a lower divider provides better resolution, so use the largest value that
// accommodates the requested frequency.

//#define FREQUENCY_HZ 1.0
#define FREQUENCY_HZ 10.0
//#define FREQUENCY_HZ 32.1
//#define FREQUENCY_HZ 51.7
//#define FREQUENCY_HZ 4998.0
//#define FREQUENCY_HZ 500000.0
//#define FREQUENCY_HZ 163700.0
//#define FREQUENCY_HZ 1637000.0
//#define FREQUENCY_HZ 16300000.0

// 8-bit RMT divider:
//#define CLOCK_DIVIDER 1    // Generate a minimum frequency of ~20.012 Hz
//#define CLOCK_DIVIDER 2    // Generate a minimum frequency of ~10.006 Hz
#define CLOCK_DIVIDER 4    // Generate a minimum frequency of ~5.003 Hz
//#define CLOCK_DIVIDER 16   // Generate a minimum frequency of ~1.251 Hz
//#define CLOCK_DIVIDER 64   // Generate a minimum frequency of ~0.313 Hz

void dco_rmt_task(void * pvParameter)
{
    double frequency = FREQUENCY_HZ;
    ESP_LOGI(TAG, "frequency %f Hz", frequency);

    // calculate the period from the requested frequency
    double period = 1.0 / frequency;
    ESP_LOGI(TAG, "period %0.6f s", period);

    // fetch the APB clock rate
    uint32_t apb_freq = get_apb_frequency_MHz() * 1000000;
    ESP_LOGI(TAG, "apb freq %d", apb_freq);

    // lower divider provides higher resolution
    uint8_t clock_divider = CLOCK_DIVIDER;
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

    // if the requested frequency is too low, print an error message and assert
    if (max_periods_per_mem_block == 0)
    {
        ESP_LOGE(TAG, "Requested frequency %f is too low for the current clock divider %d", frequency, clock_divider);
        ESP_LOGE(TAG, "Try increasing the divider value to generate lower frequencies");
        assert(max_periods_per_mem_block > 0);
    }

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
        .gpio_num = GPIO_DCO_OUT,
        .mem_block_num = 1,  // single block
        .clk_div = clock_divider,
        .tx_config.loop_en = true,
        .tx_config.carrier_en = false,
        .tx_config.idle_level = RMT_IDLE_LEVEL_LOW,
        .tx_config.idle_output_en = true,
    };
    rmt_config(&rmt_tx);
    rmt_driver_install(rmt_tx.channel, 0, 0);

    // also drive onboard LED
    gpio_pad_select_gpio(GPIO_LED);
    gpio_set_direction(GPIO_LED, GPIO_MODE_OUTPUT);
    gpio_matrix_out(GPIO_LED, RMT_SIG_OUT0_IDX + rmt_tx.channel, 0, 0);

    rmt_write_items(RMT_TX_CHANNEL, items, num_items, false);

    while(1)
        vTaskDelay(1);

    vTaskDelete(NULL);
}

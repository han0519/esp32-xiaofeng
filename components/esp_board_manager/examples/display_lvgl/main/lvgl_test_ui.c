/*
 * SPDX-FileCopyrightText: 2024-2025 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "lvgl.h"
#include "esp_lv_adapter.h"
#include "lvgl_test_ui.h"

static const char *TAG = "lvgl_test_ui";

// Button dimensions
#define TEST_BTN_WIDTH  100
#define TEST_BTN_HEIGHT 50

// Maximum result text length
#define MAX_RESULT_TEXT 512

typedef enum {
    LCD_LVGL_TEST_SUCCESS = 0,
    LCD_LVGL_TEST_FAIL    = -1,
    LCD_LVGL_TEST_ERROR   = -2,
} lcd_lvgl_test_result_t;

typedef enum {
    LCD_LVGL_COLOR_MAGENTA = 0,
    LCD_LVGL_COLOR_CYAN    = 1,
    LCD_LVGL_COLOR_BLUE    = 2,
    LCD_LVGL_COLOR_WHITE   = 3,
    LCD_LVGL_COLOR_COUNT   = 4
} lcd_lvgl_color_t;

typedef struct {
    lcd_lvgl_color_t  current_color;
    uint8_t           test_results[LCD_LVGL_COLOR_COUNT];
    lv_obj_t         *color_screen;
    lv_obj_t         *pass_btn;
    lv_obj_t         *fail_btn;
    lv_obj_t         *hint_label;
    bool              is_testing;
    bool              button_pressed;
} lcd_lvgl_test_ctx_t;

static const lv_color_t test_colors[] = {
    LV_COLOR_MAKE(255, 0, 255),
    LV_COLOR_MAKE(0, 255, 255),
    LV_COLOR_MAKE(0, 0, 255),
    LV_COLOR_MAKE(255, 255, 255),
};

static const char *color_names[] = {
    "Magenta",
    "Cyan",
    "Blue",
    "White",
};

static lcd_lvgl_test_ctx_t test_ctx;

static void ui_acquire(void)
{
    esp_lv_adapter_lock(-1);
}

static void ui_release(void)
{
    esp_lv_adapter_unlock();
}

static void cleanup_ui_elements(void)
{
    ui_acquire();

    if (test_ctx.color_screen && lv_obj_is_valid(test_ctx.color_screen)) {
        lv_obj_del(test_ctx.color_screen);
        test_ctx.color_screen = NULL;
    }

    /* Since pass_btn, fail_btn and hint_label are children of color_screen,
       they are deleted automatically when color_screen is deleted.
       Just clear pointers. */
    test_ctx.hint_label = NULL;
    test_ctx.pass_btn = NULL;
    test_ctx.fail_btn = NULL;

    ui_release();
    ESP_LOGI(TAG, "cleanup_ui_elements done");
}

static void get_screen_size(lv_coord_t *width, lv_coord_t *height)
{
    lv_display_t *disp = lv_display_get_default();
    if (disp) {
        *width = lv_display_get_horizontal_resolution(disp);
        *height = lv_display_get_vertical_resolution(disp);
    } else {
        *width = 320;
        *height = 240;
    }
}

// Pass button event callback
static void pass_btn_event_cb(lv_event_t *e)
{
    if (!e) return;

    ui_acquire();
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        if (test_ctx.is_testing) {
            if (test_ctx.current_color < LCD_LVGL_COLOR_COUNT) {
                test_ctx.test_results[test_ctx.current_color] = 1; // Pass
                ESP_LOGI(TAG, "%s screen test PASSED", color_names[test_ctx.current_color]);
            }
            test_ctx.button_pressed = true;
            test_ctx.is_testing = false;
        }
    }
    ui_release();
}

// Fail button event callback
static void fail_btn_event_cb(lv_event_t *e)
{
    if (!e) return;

    ui_acquire();
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        if (test_ctx.is_testing) {
            if (test_ctx.current_color < LCD_LVGL_COLOR_COUNT) {
                test_ctx.test_results[test_ctx.current_color] = 0; // Fail
                ESP_LOGI(TAG, "%s screen test FAILED", color_names[test_ctx.current_color]);
            }
            test_ctx.button_pressed = true;
            test_ctx.is_testing = false;
        }
    }
    ui_release();
}

static void show_test_results(bool result)
{
    ESP_LOGI(TAG, "show_test_results start");
    ui_acquire();

    lv_obj_clean(lv_scr_act());

    lv_obj_t *result_label = lv_label_create(lv_scr_act());
    if (result_label) {
        lv_obj_align(result_label, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_style_text_font(result_label, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(result_label, lv_color_black(), 0);

        char result_text[MAX_RESULT_TEXT] = {0};
        char temp[64];

        snprintf(result_text, sizeof(result_text), "LCD Test: %s\n\n", result ? "Pass" : "Fail");
        for (int i = 0; i < LCD_LVGL_COLOR_COUNT; i++) {
            snprintf(temp, sizeof(temp), "%s Screen: %s\n",
                     color_names[i],
                     test_ctx.test_results[i] ? "Pass" : "Fail");
            strcat(result_text, temp);
        }
        lv_label_set_text(result_label, result_text);
    }
    ESP_LOGI(TAG, "show_test_results UI update done");

    ui_release();

    vTaskDelay(pdMS_TO_TICKS(3000)); // Show results for 3 seconds

    ui_acquire();
    if(result_label && lv_obj_is_valid(result_label)) {
        lv_obj_del(result_label);
    }
    ui_release();
    ESP_LOGI(TAG, "show_test_results done");
}

static lcd_lvgl_test_result_t lcd_lvgl_test_run_single(lcd_lvgl_color_t color)
{
    if (!lv_scr_act()) {
        ESP_LOGE(TAG, "Screen object is NULL");
        return LCD_LVGL_TEST_ERROR;
    }

    cleanup_ui_elements();

    test_ctx.current_color = color;
    test_ctx.test_results[color] = 0;
    test_ctx.is_testing = true;
    test_ctx.button_pressed = false;

    lv_coord_t screen_width, screen_height;
    get_screen_size(&screen_width, &screen_height);

    ui_acquire();

    test_ctx.color_screen = lv_obj_create(lv_scr_act());
    lv_obj_set_size(test_ctx.color_screen, screen_width, screen_height);
    lv_obj_set_pos(test_ctx.color_screen, 0, 0);
    lv_obj_set_style_bg_color(test_ctx.color_screen, test_colors[color], 0);

    // Hint Label
    test_ctx.hint_label = lv_label_create(test_ctx.color_screen);
    lv_obj_align(test_ctx.hint_label, LV_ALIGN_TOP_MID, 0, 60);
    lv_obj_set_style_text_font(test_ctx.hint_label, &lv_font_montserrat_14, 0);

    lv_color_t text_color = (color == LCD_LVGL_COLOR_WHITE || color == LCD_LVGL_COLOR_CYAN) ? lv_color_black() : lv_color_white();
    lv_obj_set_style_text_color(test_ctx.hint_label, text_color, 0);
    lv_label_set_text(test_ctx.hint_label, "Press PASS or FAIL button");

    // Pass Button
    test_ctx.pass_btn = lv_btn_create(test_ctx.color_screen);
    lv_obj_set_size(test_ctx.pass_btn, TEST_BTN_WIDTH, TEST_BTN_HEIGHT);
    lv_obj_align(test_ctx.pass_btn, LV_ALIGN_BOTTOM_LEFT, 20, -40);
    lv_obj_set_style_bg_color(test_ctx.pass_btn, lv_color_make(0, 200, 0), 0);
    lv_obj_add_event_cb(test_ctx.pass_btn, pass_btn_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *pass_label = lv_label_create(test_ctx.pass_btn);
    lv_label_set_text(pass_label, "PASS");
    lv_obj_center(pass_label);

    // Fail Button
    test_ctx.fail_btn = lv_btn_create(test_ctx.color_screen);
    lv_obj_set_size(test_ctx.fail_btn, TEST_BTN_WIDTH, TEST_BTN_HEIGHT);
    lv_obj_align(test_ctx.fail_btn, LV_ALIGN_BOTTOM_RIGHT, -20, -40);
    lv_obj_set_style_bg_color(test_ctx.fail_btn, lv_color_make(200, 0, 0), 0);
    lv_obj_add_event_cb(test_ctx.fail_btn, fail_btn_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *fail_label = lv_label_create(test_ctx.fail_btn);
    lv_label_set_text(fail_label, "FAIL");
    lv_obj_center(fail_label);

    ui_release();

    // Wait for button press with timeout
    TickType_t start_time = xTaskGetTickCount();
    const TickType_t timeout = pdMS_TO_TICKS(3000);  // 3 seconds timeout

    while (!test_ctx.button_pressed) {
        vTaskDelay(pdMS_TO_TICKS(100));

        if ((xTaskGetTickCount() - start_time) > timeout) {
            ESP_LOGW(TAG, "Test timeout for color %d", color);
            test_ctx.test_results[color] = 0;  // Fail on timeout
            break;
        }
    }

    cleanup_ui_elements();
    return test_ctx.test_results[color] ? LCD_LVGL_TEST_SUCCESS : LCD_LVGL_TEST_FAIL;
}

void lvgl_test_start(void)
{
    ESP_LOGI(TAG, "Starting LCD LVGL test sequence");

    memset(&test_ctx, 0, sizeof(test_ctx));

    for (lcd_lvgl_color_t color = LCD_LVGL_COLOR_MAGENTA; color < LCD_LVGL_COLOR_COUNT; color++) {
        ESP_LOGI(TAG, "Testing %s screen", color_names[color]);
        lcd_lvgl_test_run_single(color);
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    bool overall_result = true;
    for (int i = 0; i < LCD_LVGL_COLOR_COUNT; i++) {
        if (!test_ctx.test_results[i]) {
            overall_result = false;
            break;
        }
    }

    show_test_results(overall_result);
    ESP_LOGI(TAG, "Test sequence completed. Result: %s.", overall_result ? "PASS" : "FAIL");
}

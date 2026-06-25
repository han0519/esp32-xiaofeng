/*
 * SPDX-FileCopyrightText: 2024-2025 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_lv_adapter.h"
#include "lvgl.h"
#include "lvgl_test_ui.h"
#include "esp_board_manager_includes.h"

static const char *TAG = "BMGR_DISPLAY_LVGL";

static esp_err_t lcd_lvgl_adapter_init(void)
{
    ESP_LOGI(TAG, "Initializing LVGL adapter...");
    esp_lv_adapter_config_t adapter_config = ESP_LV_ADAPTER_DEFAULT_CONFIG();
    adapter_config.task_priority = 4;
    adapter_config.task_stack_size = 6 * 1024;
    adapter_config.task_core_id = 1;
    adapter_config.tick_period_ms = 5;
    adapter_config.task_max_delay_ms = 500;

    esp_err_t ret = esp_lv_adapter_init(&adapter_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize LVGL adapter: %s", esp_err_to_name(ret));
        return ESP_FAIL;
    }
    return ESP_OK;
}

#if CONFIG_ESP_BOARD_DEV_DISPLAY_LCD_SUB_DSI_SUPPORT || CONFIG_ESP_BOARD_DEV_DISPLAY_LCD_SUB_RGB_SUPPORT || CONFIG_ESP_BOARD_DEV_DISPLAY_LCD_SUB_RGB_3WIRE_SPI_SUPPORT
static esp_lv_adapter_rotation_t lcd_get_rotation(const dev_display_lcd_config_t *lcd_cfg)
{
    if (lcd_cfg->swap_xy) {
        return lcd_cfg->mirror_x ? ESP_LV_ADAPTER_ROTATE_90 : ESP_LV_ADAPTER_ROTATE_270;
    }

    return (lcd_cfg->mirror_x || lcd_cfg->mirror_y) ? ESP_LV_ADAPTER_ROTATE_180 : ESP_LV_ADAPTER_ROTATE_0;
}

static esp_lv_adapter_tear_avoid_mode_t lcd_get_tear_mode(uint8_t num_fbs)
{
    if (num_fbs >= 3) {
        return ESP_LV_ADAPTER_TEAR_AVOID_MODE_TRIPLE_PARTIAL;
    }
    if (num_fbs == 2) {
        return ESP_LV_ADAPTER_TEAR_AVOID_MODE_DOUBLE_PARTIAL;
    }
    return ESP_LV_ADAPTER_TEAR_AVOID_MODE_NONE;
}
#endif  /* CONFIG_ESP_BOARD_DEV_DISPLAY_LCD_SUB_DSI_SUPPORT || CONFIG_ESP_BOARD_DEV_DISPLAY_LCD_SUB_RGB_SUPPORT || CONFIG_ESP_BOARD_DEV_DISPLAY_LCD_SUB_RGB_3WIRE_SPI_SUPPORT */

static lv_display_t *lcd_lvgl_adapter_register_display(const dev_display_lcd_config_t *lcd_cfg,
                                                       const dev_display_lcd_handles_t *lcd_handles)
{
    esp_lv_adapter_rotation_t rotation = ESP_LV_ADAPTER_ROTATE_0;
    esp_lv_adapter_display_config_t disp_cfg;

    if (strcmp(lcd_cfg->sub_type, ESP_BOARD_DEVICE_LCD_SUB_TYPE_DSI) == 0) {
#if CONFIG_ESP_BOARD_DEV_DISPLAY_LCD_SUB_DSI_SUPPORT
        rotation = lcd_get_rotation(lcd_cfg);
        disp_cfg = ESP_LV_ADAPTER_DISPLAY_MIPI_DEFAULT_CONFIG(lcd_handles->panel_handle,
                                                              lcd_handles->io_handle,
                                                              lcd_cfg->lcd_width,
                                                              lcd_cfg->lcd_height,
                                                              rotation);
        disp_cfg.tear_avoid_mode = lcd_get_tear_mode(lcd_cfg->sub_cfg.dsi.dpi_config.num_fbs);
#else
        ESP_LOGE(TAG, "DSI support not enabled");
        return NULL;
#endif  /* CONFIG_ESP_BOARD_DEV_DISPLAY_LCD_SUB_DSI_SUPPORT */
    } else if (strcmp(lcd_cfg->sub_type, ESP_BOARD_DEVICE_LCD_SUB_TYPE_RGB) == 0 ||
               strcmp(lcd_cfg->sub_type, ESP_BOARD_DEVICE_LCD_SUB_TYPE_RGB_3WIRE_SPI) == 0) {
#if CONFIG_ESP_BOARD_DEV_DISPLAY_LCD_SUB_RGB_SUPPORT || CONFIG_ESP_BOARD_DEV_DISPLAY_LCD_SUB_RGB_3WIRE_SPI_SUPPORT
        rotation = lcd_get_rotation(lcd_cfg);
        disp_cfg = ESP_LV_ADAPTER_DISPLAY_RGB_DEFAULT_CONFIG(lcd_handles->panel_handle,
                                                             lcd_handles->io_handle,
                                                             lcd_cfg->lcd_width,
                                                             lcd_cfg->lcd_height,
                                                             rotation);
        if (strcmp(lcd_cfg->sub_type, ESP_BOARD_DEVICE_LCD_SUB_TYPE_RGB_3WIRE_SPI) == 0) {
#if CONFIG_ESP_BOARD_DEV_DISPLAY_LCD_SUB_RGB_3WIRE_SPI_SUPPORT
            disp_cfg.tear_avoid_mode = lcd_get_tear_mode(lcd_cfg->sub_cfg.rgb_3wire_spi.rgb_panel_config.num_fbs);
#endif  /* CONFIG_ESP_BOARD_DEV_DISPLAY_LCD_SUB_RGB_3WIRE_SPI_SUPPORT */
        } else {
#if CONFIG_ESP_BOARD_DEV_DISPLAY_LCD_SUB_RGB_SUPPORT
            disp_cfg.tear_avoid_mode = lcd_get_tear_mode(lcd_cfg->sub_cfg.rgb.panel_config.num_fbs);
#endif  /* CONFIG_ESP_BOARD_DEV_DISPLAY_LCD_SUB_RGB_SUPPORT */
        }
#else
        ESP_LOGE(TAG, "RGB support not enabled");
        return NULL;
#endif  /* CONFIG_ESP_BOARD_DEV_DISPLAY_LCD_SUB_RGB_SUPPORT || CONFIG_ESP_BOARD_DEV_DISPLAY_LCD_SUB_RGB_3WIRE_SPI_SUPPORT */
    } else if (strcmp(lcd_cfg->sub_type, ESP_BOARD_DEVICE_LCD_SUB_TYPE_SPI) == 0 ||
               strcmp(lcd_cfg->sub_type, ESP_BOARD_DEVICE_LCD_SUB_TYPE_I80) == 0 ||
               strcmp(lcd_cfg->sub_type, ESP_BOARD_DEVICE_LCD_SUB_TYPE_PARLIO) == 0) {
#ifdef CONFIG_SPIRAM
        disp_cfg = ESP_LV_ADAPTER_DISPLAY_SPI_WITH_PSRAM_DEFAULT_CONFIG(lcd_handles->panel_handle,
                                                                        lcd_handles->io_handle,
                                                                        lcd_cfg->lcd_width,
                                                                        lcd_cfg->lcd_height,
                                                                        rotation);
#else
        disp_cfg = ESP_LV_ADAPTER_DISPLAY_SPI_WITHOUT_PSRAM_DEFAULT_CONFIG(lcd_handles->panel_handle,
                                                                           lcd_handles->io_handle,
                                                                           lcd_cfg->lcd_width,
                                                                           lcd_cfg->lcd_height,
                                                                           rotation);
#endif  /* CONFIG_SPIRAM */
    } else {
        ESP_LOGE(TAG, "Unknown LCD sub_type: %s", lcd_cfg->sub_type);
        return NULL;
    }

    ESP_LOGI(TAG, "Register LCD to LVGL adapter: sub_type=%s, size=%dx%d, rotation=%d, tear_mode=%d",
             lcd_cfg->sub_type, lcd_cfg->lcd_width, lcd_cfg->lcd_height, rotation, disp_cfg.tear_avoid_mode);

    return esp_lv_adapter_register_display(&disp_cfg);
}

void app_main(void)
{
    ESP_LOGI(TAG, "Starting LVGL Example");

    esp_err_t ret = esp_board_manager_init_device_by_name(ESP_BOARD_DEVICE_NAME_DISPLAY_LCD);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize display_lcd: %s", esp_err_to_name(ret));
        return;
    }

    ret = esp_board_manager_init_device_by_name(ESP_BOARD_DEVICE_NAME_LCD_TOUCH);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to initialize lcd_touch: %s, continue", esp_err_to_name(ret));
    }

    ret = esp_board_manager_init_device_by_name(ESP_BOARD_DEVICE_NAME_LCD_BRIGHTNESS);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to initialize lcd_brightness: %s, continue", esp_err_to_name(ret));
    }

    /* Initialize LVGL adapter */
    if (lcd_lvgl_adapter_init() != ESP_OK) {
        goto cleanup_devices;
    }

    /* Initialize LCD Display */
    void *lcd_handle = NULL;
    ret = esp_board_manager_get_device_handle(ESP_BOARD_DEVICE_NAME_DISPLAY_LCD, &lcd_handle);
    if (ret != ESP_OK || !lcd_handle) {
        ESP_LOGE(TAG, "Failed to get LCD device handle");
        goto cleanup_adapter;
    }

    /* Get LCD Configuration */
    dev_display_lcd_config_t *lcd_cfg = NULL;
    ret = esp_board_manager_get_device_config("display_lcd", (void **)&lcd_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get LCD config");
        goto cleanup_adapter;
    }

    /* Add display to LVGL adapter */
    dev_display_lcd_handles_t *lcd_handles = (dev_display_lcd_handles_t *)lcd_handle;
    lv_display_t *disp = lcd_lvgl_adapter_register_display(lcd_cfg, lcd_handles);
    if (!disp) {
        ESP_LOGE(TAG, "Failed to register display to LVGL adapter");
        goto cleanup_adapter;
    }

    /* Initialize Touch (Optional) */
    lv_indev_t *touch_indev = NULL;
#ifdef CONFIG_ESP_BOARD_DEV_LCD_TOUCH_SUPPORT
    void *touch_handle = NULL;
    if (esp_board_manager_get_device_handle(ESP_BOARD_DEVICE_NAME_LCD_TOUCH, &touch_handle) == ESP_OK && touch_handle) {
        dev_lcd_touch_handles_t *touch_handles = (dev_lcd_touch_handles_t *)touch_handle;
        const esp_lv_adapter_touch_config_t touch_cfg = ESP_LV_ADAPTER_TOUCH_DEFAULT_CONFIG(disp, touch_handles->touch_handle);
        if ((touch_indev = esp_lv_adapter_register_touch(&touch_cfg)) == NULL) {
            ESP_LOGW(TAG, "Failed to add touch input");
        } else {
            ESP_LOGI(TAG, "Touch input added");
        }
    }
#endif  /* CONFIG_ESP_BOARD_DEV_LCD_TOUCH_SUPPORT */

    ret = esp_lv_adapter_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start LVGL adapter: %s", esp_err_to_name(ret));
        goto cleanup_lvgl;
    }

    /* Run Test UI */
    ESP_LOGI(TAG, "Running LVGL Test UI");
    lvgl_test_start();

cleanup_lvgl:
    /* Deinitialize LVGL adapter */
    if (touch_indev) {
        esp_lv_adapter_unregister_touch(touch_indev);
    }
    if (disp) {
        esp_lv_adapter_unregister_display(disp);
    }

cleanup_adapter:
    esp_lv_adapter_deinit();

    ESP_LOGI(TAG, "Example Finished. Exiting app_main...");

cleanup_devices:
    esp_board_manager_deinit_device_by_name(ESP_BOARD_DEVICE_NAME_LCD_BRIGHTNESS);
    esp_board_manager_deinit_device_by_name(ESP_BOARD_DEVICE_NAME_DISPLAY_LCD);
    esp_board_manager_deinit_device_by_name(ESP_BOARD_DEVICE_NAME_LCD_TOUCH);
}

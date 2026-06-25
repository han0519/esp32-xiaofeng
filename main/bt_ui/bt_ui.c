/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <inttypes.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"
#include "esp_board_manager.h"
#include "esp_board_manager_defs.h"
#include "dev_display_lcd.h"
#include "bt_ui.h"
#include "jpeg_decoder.h"
#include "esp_bt_audio_defs.h"
#include "esp_bt_audio_playback.h"
#include "esp_bt_audio_tel.h"
#include "driver/i2c_master.h"
#include "gallery_images.h"

/* GT1151 touch controller constants */
#define GT1151_I2C_ADDR_7BIT   0x14
#define GT1151_REG_CMD         0x8040
#define GT1151_REG_CFG         0x8047
#define GT1151_REG_TOUCH1      0x814E
#define GT1151_REG_PRODUCT_ID  0x8140
#define GT1151_READ_CMD_LEN    2
#define GT1151_COORD_OFFSET    1
#define GT1151_POINT_SIZE      8
#define GT1151_MAX_TOUCH_POINTS 5

#define UI_COVER_QUEUE_SIZE       2
#define UI_COVER_TASK_STACK_SIZE  4096
#define UI_COVER_TASK_PRIO        3   /* Lower than audio to avoid stutter */
#define LVGL_DRAW_BUF_LINES       (BT_UI_HEIGHT / 4)   /* Smaller buffer reduces PSRAM contention during audio */

/* Spectrum visualizer */
#define SPECTRUM_BAR_COUNT       16          /* Number of bars */
#define SPECTRUM_W               300         /* Total width */
#define SPECTRUM_H               64          /* Total height */
#define SPECTRUM_BAR_W           (SPECTRUM_W / SPECTRUM_BAR_COUNT - 2)  /* Bar width */
#define SPECTRUM_X               8           /* Bottom-left X */
#define BTN_ROW_X_RIGHT         (BT_UI_WIDTH - 260)         /* Button row start X */
#define BTN_ROW_Y_BOTTOM        (BT_UI_HEIGHT - BTN_ROW_H - 16)  /* Button row Y */
#define SPECTRUM_UPDATE_MS       40          /* Update period (25fps) */

#define BT_UI_FONT_LARGE       (&lv_font_notosanssc_regular_28)
#define BT_UI_FONT_MEDIUM      (&lv_font_notosanssc_regular_28)
#define BT_UI_FONT_SMALL       (&lv_font_notosanssc_regular_28)
#define BT_UI_FONT_ICON        (&lv_font_montserrat_28)
#define BT_UI_FONT_COVER_ICON  (&lv_font_montserrat_32)

/* Landscape (800x480) layout - Circular cover art + info panel + gallery */
#define COVER_SIZE         400         /* Cover art size (circular clipping) */
#define COVER_X            0           /* Cover at left edge — zero blank space */
#define COVER_Y            20          /* Cover top Y */
#define COVER_CENTER_X     (COVER_X + COVER_SIZE / 2)  /* Cover center X */
#define COVER_CENTER_Y     (COVER_Y + COVER_SIZE / 2)  /* Cover center Y */
#define COVER_RADIUS       (COVER_SIZE / 2)  /* Circular clip radius */
#define RIGHT_PANEL_X      420         /* Right info panel start X (COVER_X+COVER_SIZE+20) */
#define RIGHT_PANEL_W      (BT_UI_WIDTH - RIGHT_PANEL_X - 12)  /* = 368 */
#define LYRIC_AREA_H       280         /* Lyrics scroll area height */
#define BTN_ROW_H           72
#define BTN_W               80
#define BTN_H               60
#define DIALER_KEYPAD_SZ    92
#define DIALER_KEY_GAP      16

#define CONTENT_H           BT_UI_HEIGHT
#define VOL_BAR_W           6
#define VOL_BAR_H           (BT_UI_HEIGHT / 5)
#define VOL_BAR_RIGHT_GAP   12
LV_FONT_DECLARE(lv_font_notosanssc_regular_28)

#if CONFIG_GMF_EXAMPLE_AUDIO_TECH_LE
LV_IMAGE_DECLARE(speakers_dark_dark_small);
LV_IMAGE_DECLARE(speakers_dark_light_small);
LV_IMAGE_DECLARE(speakers_light_dark_small);
LV_IMAGE_DECLARE(cis_stream_icon);
LV_IMAGE_DECLARE(bis_stream_icon);

/* Source art is 440x260; keep aspect when fitting the zone below the title. */
#define SPEAKER_SRC_W      440
#define SPEAKER_SRC_H      260
#define LE_TITLE_AREA_H    96
#define LE_SPEAKER_ZONE_W  (RIGHT_PANEL_W - 16)
#define LE_SPEAKER_ZONE_H  (COVER_SIZE - LE_TITLE_AREA_H - BTN_ROW_HEIGHT)
#if (LE_SPEAKER_ZONE_W * SPEAKER_SRC_H / SPEAKER_SRC_W) <= LE_SPEAKER_ZONE_H
#define SPEAKER_IMG_W  LE_SPEAKER_ZONE_W
#define SPEAKER_IMG_H  (LE_SPEAKER_ZONE_W * SPEAKER_SRC_H / SPEAKER_SRC_W)
#else
#define SPEAKER_IMG_H  LE_SPEAKER_ZONE_H
#define SPEAKER_IMG_W  (LE_SPEAKER_ZONE_H * SPEAKER_SRC_W / SPEAKER_SRC_H)
#endif  /* (LE_SPEAKER_ZONE_W * SPEAKER_SRC_H / SPEAKER_SRC_W) <= LE_SPEAKER_ZONE_H */
#endif  /* CONFIG_GMF_EXAMPLE_AUDIO_TECH_LE */

#define DIALER_BUF_SIZE  32

/**
 * @brief  Cover image payload queued to the UI task.
 */
typedef struct {
    uint8_t  *data;  /*!< Encoded image buffer owned by the UI task */
    size_t    size;  /*!< Encoded image buffer size in bytes */
} ui_cover_msg_t;

/**
 * @brief  Runtime state for the Bluetooth demo UI.
 */
struct bt_ui_t {
    lv_disp_t                    *disp;                      /*!< Display instance used by LVGL */
    lv_obj_t                     *splash;                    /*!< Splash screen root object */
    lv_obj_t                     *main;                      /*!< Main UI root object */
    lv_obj_t                     *tabview;                   /*!< Main tabview (tab 0=gallery, tab 1=music, tab 2=dialer) */
    lv_obj_t                     *media;                     /*!< Media page root object */
    lv_obj_t                     *dialer;                    /*!< Dialer page root object */
    lv_obj_t                     *gallery;                   /*!< Gallery page root object */
    lv_obj_t                     *volume_bar;                /*!< Floating volume indicator */
    int                           volume;                    /*!< Tracked volume level */
    esp_bt_audio_stream_handle_t  stream_type_stream;        /*!< Stream used to derive media placeholder state */
    bool                          stream_type_is_broadcast;  /*!< True when stream_type_stream is a BIS stream */
    QueueHandle_t                 cover_queue;               /*!< Queue carrying cover image updates */
    void                        (*gallery_active_cb)(void *ctx);  /*!< Gallery page activation callback */
    void                         *gallery_ctx;               /*!< Context for gallery callback */
    lv_color_t                    global_bg_color;            /*!< Global background color synced to cover */
};

/**
 * @brief  Cached LVGL widgets used by the dialer page.
 */
typedef struct {
    lv_obj_t *number_label;    /*!< Dialed number and call-state label */
    lv_obj_t *call_btn;        /*!< Dial/end-call button */
    lv_obj_t *call_btn_label;  /*!< Icon label inside call_btn */
    lv_obj_t *answer_btn;      /*!< Answer button shown for incoming calls */
    lv_obj_t *back_btn;        /*!< Backspace button */
    void     *call_ctx;        /*!< Pointer to dialer_call_ctx_t */
} bt_ui_dialer_refs_t;

/**
 * @brief  Dialer callback context and current call state.
 */
typedef struct {
    void (*call_cb)(const char *number, void *ctx);  /*!< Dial callback */
    void *call_cb_ctx;                               /*!< Context passed to call_cb */
    void (*end_call_cb)(void *ctx);                  /*!< End-call callback */
    void *end_call_cb_ctx;                           /*!< Context passed to end_call_cb */
    void (*answer_call_cb)(void *ctx);               /*!< Answer-call callback */
    void *answer_call_cb_ctx;                        /*!< Context passed to answer_call_cb */
    bool  in_call;                                   /*!< True while the dialer is in a call state */
} dialer_call_ctx_t;

/**
 * @brief  Cached LVGL widgets and callback state used by the media page.
 */
typedef struct {
    lv_obj_t *play_btn;               /*!< Play/pause button */
    lv_obj_t *title_label;            /*!< Track title label */
    lv_obj_t *artist_label;           /*!< Track artist label */
    lv_obj_t *album_label;            /*!< Album name label */
    lv_obj_t *cover_clip;             /*!< Circular clipping container for cover */
    lv_obj_t *cover_img;              /*!< Cover art image (spins inside clip container) */
    lv_anim_t cover_anim;             /*!< Rotation animation for cover */
    bool     cover_spinning;          /*!< Is the cover currently spinning */
    /* Lyrics */
    lv_obj_t *lyric_cont;             /*!< Lyrics container */
    lv_obj_t *lyric_lines[5];         /*!< Current visible lyric lines */
    lv_timer_t *lyric_timer;          /*!< Lyrics auto-cycle timer */
    /* Cover art buffer */
    uint8_t  *cover_data;             /*!< Decoded cover-art buffer */
    size_t    cover_size;             /*!< Decoded cover-art buffer size */
#if LVGL_VERSION_MAJOR >= 9
    lv_image_dsc_t  cover_dsc;      /*!< Persistent descriptor for LVGL (src points here) */
#else
    lv_img_dsc_t  cover_img_dsc;   /*!< Persistent descriptor for LVGL (src points here) */
#endif  /* LVGL_VERSION_MAJOR >= 9 */
    void (*play_pause_cb)(bool want_play, void *ctx);  /*!< Play/pause callback */
    void *play_pause_ctx;                              /*!< Context passed to play_pause_cb */
    void (*prev_cb)(void *ctx);                        /*!< Previous-track callback */
    void (*next_cb)(void *ctx);                        /*!< Next-track callback */
    void *prev_next_ctx;                               /*!< Context passed to prev_cb and next_cb */
    lv_color_t bg_color;                               /*!< Current background color */
    lv_obj_t *spectrum_cont;                           /*!< Spectrum visualizer container */
    lv_timer_t *spectrum_timer;                        /*!< Spectrum animation timer */
    int8_t     spectrum_heights[SPECTRUM_BAR_COUNT];   /*!< Current bar heights */
    bt_ui_t  *ui_owner;                                /*!< Back-pointer to bt_ui_t for global bg sync */
} bt_ui_media_refs_t;

static char dialer_number_buf[DIALER_BUF_SIZE];
static lv_obj_t *dialer_number_label;
static lv_timer_t *s_vol_bar_hide_timer = NULL;
static const char *TAG = "BT_UI";

/* Forward declaration — bt_ui_t defined at ~line 113 */
static void bt_ui_sync_global_bg(struct bt_ui_t *ui);

#if CONFIG_GMF_EXAMPLE_AUDIO_TECH_LE
static const lv_image_dsc_t *speaker_img_for_location(uint32_t location)
{
    bool left = (location & ESP_BT_AUDIO_AUDIO_LOC_FRONT_LEFT) != 0;
    bool right = (location & ESP_BT_AUDIO_AUDIO_LOC_FRONT_RIGHT) != 0;
    if (left && right) {
        return &speakers_dark_dark_small;
    }
    if (left) {
        return &speakers_dark_light_small;
    }
    if (right) {
        return &speakers_light_dark_small;
    }
    return &speakers_dark_dark_small;
}
#endif  /* CONFIG_GMF_EXAMPLE_AUDIO_TECH_LE */

static void vol_bar_hide_timer_cb(lv_timer_t *t)
{
    lv_obj_t *bar = (lv_obj_t *)lv_timer_get_user_data(t);
    if (bar != NULL) {
        lv_obj_add_flag(bar, LV_OBJ_FLAG_HIDDEN);
    }
    lv_timer_del(t);
    s_vol_bar_hide_timer = NULL;
}

lv_obj_t *bt_ui_volume_bar_create(lv_obj_t *parent)
{
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_remove_style_all(cont);
    lv_obj_set_size(cont, VOL_BAR_W, VOL_BAR_H);
    lv_obj_align(cont, LV_ALIGN_RIGHT_MID, -VOL_BAR_RIGHT_GAP, 0);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(cont, 0, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_radius(cont, 2, 0);
    lv_obj_set_style_bg_opa(cont, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(cont, lv_color_hex(0x505060), 0);

    lv_obj_t *fill = lv_obj_create(cont);
    lv_obj_remove_style_all(fill);
    lv_obj_set_width(fill, VOL_BAR_W);
    lv_obj_set_height(fill, 0);
    lv_obj_align(fill, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_clear_flag(fill, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(fill, 2, 0);
    lv_obj_set_style_bg_opa(fill, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(fill, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_border_width(fill, 0, 0);
    lv_obj_set_style_pad_all(fill, 0, 0);

    lv_obj_set_user_data(cont, fill);
    lv_obj_add_flag(cont, LV_OBJ_FLAG_HIDDEN);
    return cont;
}

void bt_ui_volume_bar_set_level(lv_obj_t *vol_bar, int level_percent)
{
    if (vol_bar == NULL) {
        return;
    }
    if (level_percent < 0) {
        level_percent = 0;
    }
    if (level_percent > 100) {
        level_percent = 100;
    }
    lv_obj_t *fill = (lv_obj_t *)lv_obj_get_user_data(vol_bar);
    if (fill == NULL) {
        return;
    }
    int32_t h = lv_obj_get_height(vol_bar);
    int32_t fill_h = (int32_t)((int64_t)h * level_percent / 100);
    if (fill_h < 0) {
        fill_h = 0;
    }
    lv_obj_set_height(fill, (lv_coord_t)fill_h);
    lv_obj_align(fill, LV_ALIGN_BOTTOM_MID, 0, 0);

    lv_obj_clear_flag(vol_bar, LV_OBJ_FLAG_HIDDEN);
    if (s_vol_bar_hide_timer != NULL) {
        lv_timer_reset(s_vol_bar_hide_timer);
    } else {
        s_vol_bar_hide_timer = lv_timer_create(vol_bar_hide_timer_cb, 1000, vol_bar);
        lv_timer_set_repeat_count(s_vol_bar_hide_timer, 1);
    }
}

static void bt_ui_cover_task(void *arg)
{
    bt_ui_t *ui = (bt_ui_t *)arg;
    ui_cover_msg_t msg = {0};
    while (true) {
        if (xQueueReceive(ui->cover_queue, &msg, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        /* Clear cover request (NULL data) */
        if (ui->media != NULL && msg.data == NULL) {
            lvgl_port_lock(0);
            bt_ui_media_set_cover_image(ui->media, NULL, 0);
            lvgl_port_unlock();
            continue;
        }
        if (ui->media != NULL && msg.data != NULL && msg.size > 0) {
            lvgl_port_lock(0);
            bt_ui_media_set_cover_image(ui->media, msg.data, msg.size);
            /* Global bg sync handled inside bt_ui_media_set_cover_image via ui_owner */
            lvgl_port_unlock();
        } else if (msg.data != NULL) {
            free(msg.data);
        }
    }
}

/**
 * @brief  LVGL touchpad read callback for GT1151 over I2C
 *
 *         Reads touch data directly from GT1151 register 0x814E via I2C.
 *         Reports the first touch point coordinates to LVGL.
 */
static void bt_ui_gt1151_touch_read(lv_indev_t *indev_drv, lv_indev_data_t *data)
{
    static uint32_t tick_cnt = 0;
    i2c_master_dev_handle_t gt1151_dev = (i2c_master_dev_handle_t)lv_indev_get_driver_data(indev_drv);
    if (gt1151_dev == NULL) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    /* Read touch status + first touch point data (9 bytes from 0x814E) */
    uint8_t reg_addr[2] = {(GT1151_REG_TOUCH1 >> 8) & 0xFF, GT1151_REG_TOUCH1 & 0xFF};
    uint8_t buf[1 + GT1151_POINT_SIZE] = {0};

    esp_err_t ret = i2c_master_transmit_receive(gt1151_dev, reg_addr, sizeof(reg_addr),
                                                 buf, sizeof(buf), 10);
    if (ret != ESP_OK) {
        data->state = LV_INDEV_STATE_RELEASED;
        if ((tick_cnt++ % 500) == 0) {
            ESP_LOGW(TAG, "GT1151 I2C read failed: %s", esp_err_to_name(ret));
        }
        return;
    }

    /* Check buffer status bit (bit 7): 0 = updating, 1 = ready */
    uint8_t buf_ready = buf[0] & 0x80;
    uint8_t touch_cnt = buf[0] & 0x0F;

    if ((tick_cnt++ % 200) == 0) {
        ESP_LOGI(TAG, "GT1151 status=0x%02X ready=%d cnt=%d raw=%02X %02X %02X %02X %02X %02X %02X %02X %02X",
                 buf[0], !!buf_ready, touch_cnt,
                 buf[1], buf[2], buf[3], buf[4], buf[5], buf[6], buf[7], buf[8]);
    }

    if (buf_ready && touch_cnt > 0 && touch_cnt <= GT1151_MAX_TOUCH_POINTS) {
        /* GT1151 data is little-endian per point (8 bytes):
         *   buf[1]=TrackID, buf[2]=X_L, buf[3]=X_H, buf[4]=Y_L, buf[5]=Y_H, buf[6]=Size_L, buf[7]=Size_H, buf[8]=Reserved
         *   X = (X_H << 8) | X_L, Y = (Y_H << 8) | Y_L */
        uint16_t x = ((uint16_t)buf[GT1151_COORD_OFFSET + 2] << 8) | buf[GT1151_COORD_OFFSET + 1];
        uint16_t y = ((uint16_t)buf[GT1151_COORD_OFFSET + 4] << 8) | buf[GT1151_COORD_OFFSET + 3];
        data->point.x = (int32_t)x;
        data->point.y = (int32_t)y;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }

    /* Clear touch buffer status only when data was ready */
    if (buf_ready) {
        uint8_t clear_cmd[3] = {(GT1151_REG_TOUCH1 >> 8) & 0xFF, GT1151_REG_TOUCH1 & 0xFF, 0x00};
        i2c_master_transmit(gt1151_dev, clear_cmd, sizeof(clear_cmd), 5);
    }
}

esp_err_t bt_ui_init(void)
{
    dev_display_lcd_handles_t *lcd_handles = NULL;
    dev_display_lcd_config_t *lcd_cfg = NULL;
    esp_err_t ret = esp_board_manager_get_device_handle(ESP_BOARD_DEVICE_NAME_DISPLAY_LCD, (void **)&lcd_handles);
    if (ret != ESP_OK || lcd_handles == NULL) {
        ESP_LOGE(TAG, "Failed to get LCD handle: %s", esp_err_to_name(ret));
        return ret == ESP_OK ? ESP_ERR_INVALID_STATE : ret;
    }
    ret = esp_board_manager_get_device_config(ESP_BOARD_DEVICE_NAME_DISPLAY_LCD, (void **)&lcd_cfg);
    if (ret != ESP_OK || lcd_cfg == NULL) {
        ESP_LOGE(TAG, "Failed to get LCD config: %s", esp_err_to_name(ret));
        return ret == ESP_OK ? ESP_ERR_INVALID_STATE : ret;
    }

    const lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_RETURN_ON_ERROR(lvgl_port_init(&lvgl_cfg), TAG, "LVGL port init failed");

    lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = lcd_handles->io_handle,
        .panel_handle = lcd_handles->panel_handle,
        .buffer_size = lcd_cfg->lcd_width * LVGL_DRAW_BUF_LINES,
        .double_buffer = true,
        .hres = lcd_cfg->lcd_width,
        .vres = lcd_cfg->lcd_height,
        .monochrome = false,
        .rotation = {
            .swap_xy = lcd_cfg->swap_xy,
            .mirror_x = lcd_cfg->mirror_x,
            .mirror_y = lcd_cfg->mirror_y,
        },
#if LVGL_VERSION_MAJOR >= 9
        .color_format = LV_COLOR_FORMAT_RGB565,
#endif  /* LVGL_VERSION_MAJOR >= 9 */
        .flags = {
            .buff_dma = false,
            .buff_spiram = true,
        },
    };

    lv_disp_t *disp = NULL;
    if (strcmp(lcd_cfg->sub_type, "rgb") == 0) {
#ifdef CONFIG_ESP_BOARD_DEV_DISPLAY_LCD_SUB_RGB_SUPPORT
        /* Use bounce buffer to avoid tearing when num_fbs == 1 */
        const lvgl_port_display_rgb_cfg_t rgb_cfg = {
            .flags = {
                .bb_mode = true,
                .avoid_tearing = lcd_cfg->sub_cfg.rgb.panel_config.num_fbs > 1,
            },
        };
        disp_cfg.flags.direct_mode = rgb_cfg.flags.avoid_tearing;
        disp = lvgl_port_add_disp_rgb(&disp_cfg, &rgb_cfg);
#endif  /* CONFIG_ESP_BOARD_DEV_DISPLAY_LCD_SUB_RGB_SUPPORT */
    } else {
        disp = lvgl_port_add_disp(&disp_cfg);
    }
    if (disp == NULL) {
        ESP_LOGE(TAG, "Failed to add LVGL display");
        return ESP_FAIL;
    }

    /* Initialize GT1151 touch via I2C directly (esp_lcd_touch framework not available in IDF v6.2) */
    void *i2c_handle = NULL;
    ret = esp_board_manager_get_periph_handle("i2c_master", &i2c_handle);
    if (ret == ESP_OK && i2c_handle != NULL) {
        i2c_master_bus_handle_t i2c_bus = (i2c_master_bus_handle_t)i2c_handle;
        i2c_device_config_t gt1151_dev_cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = GT1151_I2C_ADDR_7BIT,
            .scl_speed_hz = 400000,
        };
        i2c_master_dev_handle_t gt1151_dev = NULL;
        if (i2c_master_bus_add_device(i2c_bus, &gt1151_dev_cfg, &gt1151_dev) == ESP_OK && gt1151_dev != NULL) {
            /* Verify GT1151 by reading product ID */
            uint8_t reg_addr[2] = {(GT1151_REG_PRODUCT_ID >> 8) & 0xFF, GT1151_REG_PRODUCT_ID & 0xFF};
            uint8_t prod_id[4] = {0};
            if (i2c_master_transmit_receive(gt1151_dev, reg_addr, sizeof(reg_addr), prod_id, sizeof(prod_id), 50) == ESP_OK) {
                ESP_LOGI(TAG, "GT1151 product ID: %02X %02X %02X %02X", prod_id[0], prod_id[1], prod_id[2], prod_id[3]);
            }
            /* Read config version from 0x8047 to verify chip is alive */
            uint8_t cfg_reg[2] = {(GT1151_REG_CFG >> 8) & 0xFF, GT1151_REG_CFG & 0xFF};
            uint8_t cfg_ver[2] = {0};
            if (i2c_master_transmit_receive(gt1151_dev, cfg_reg, sizeof(cfg_reg), cfg_ver, sizeof(cfg_ver), 50) == ESP_OK) {
                ESP_LOGI(TAG, "GT1151 config version: %u (0x%02X%02X)", cfg_ver[0], cfg_ver[1], cfg_ver[0], cfg_ver[1]);
            }
            /* Send start-scanning command (0x00 to 0x8040) to ensure GT1151 is in active mode */
            uint8_t start_cmd[3] = {(GT1151_REG_CMD >> 8) & 0xFF, GT1151_REG_CMD & 0xFF, 0x00};
            esp_err_t cmd_ret = i2c_master_transmit(gt1151_dev, start_cmd, sizeof(start_cmd), 50);
            if (cmd_ret == ESP_OK) {
                ESP_LOGI(TAG, "GT1151 start-scan command sent OK");
            } else {
                ESP_LOGW(TAG, "GT1151 start-scan command failed: %s", esp_err_to_name(cmd_ret));
            }
            /* Register LVGL touch input device */
            lv_indev_t *indev = lv_indev_create();
            lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
            lv_indev_set_read_cb(indev, bt_ui_gt1151_touch_read);
            lv_indev_set_disp(indev, disp);
            lv_indev_set_driver_data(indev, gt1151_dev);
            ESP_LOGI(TAG, "GT1151 touch initialized successfully");
        } else {
            ESP_LOGW(TAG, "Failed to add GT1151 I2C device");
        }
    } else {
        ESP_LOGW(TAG, "I2C bus not available for touch: %s", esp_err_to_name(ret));
    }
    return ESP_OK;
}

static void tabview_value_changed_cb(lv_event_t *e)
{
    lv_obj_t *tabview = (lv_obj_t *)lv_event_get_target(e);
    bt_ui_t *ui = (bt_ui_t *)lv_event_get_user_data(e);
    if (ui == NULL || ui->gallery_active_cb == NULL) {
        return;
    }
    uint32_t cur = lv_tabview_get_tab_active(tabview);
    if (cur == BT_UI_TAB_GALLERY) {
        ui->gallery_active_cb(ui->gallery_ctx);
    }
}

bt_ui_t *bt_ui_create(const char *device_name, const bt_ui_config_t *config)
{
    bt_ui_t *ui = calloc(1, sizeof(bt_ui_t));
    if (ui == NULL) {
        ESP_LOGE(TAG, "Failed to allocate UI handle");
        return NULL;
    }
    ui->volume = 50;
    ui->global_bg_color = lv_color_hex(0x102840);

    /* Cover-art queue */
    ui->cover_queue = xQueueCreate(UI_COVER_QUEUE_SIZE, sizeof(ui_cover_msg_t));
    if (ui->cover_queue == NULL) {
        ESP_LOGW(TAG, "Create UI cover queue failed");
        free(ui);
        return NULL;
    }
    BaseType_t task_ret = xTaskCreate(bt_ui_cover_task, "ui_cover", UI_COVER_TASK_STACK_SIZE,
                                      ui, UI_COVER_TASK_PRIO, NULL);
    if (task_ret != pdPASS) {
        vQueueDelete(ui->cover_queue);
        free(ui);
        ESP_LOGW(TAG, "Create UI cover task failed");
        return NULL;
    }

    lvgl_port_lock(0);
    lv_obj_t *scr = lv_scr_act();
    /* Remove screen default padding so content fills edge-to-edge */
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_set_style_border_width(scr, 0, 0);
    ui->splash = bt_ui_splash_create(scr, device_name);

    ui->main = lv_obj_create(scr);
    lv_obj_remove_style_all(ui->main);
    lv_obj_set_size(ui->main, BT_UI_WIDTH, BT_UI_HEIGHT);
    lv_obj_align(ui->main, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_clear_flag(ui->main, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(ui->main, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(ui->main, LV_OBJ_FLAG_HIDDEN);

    /* Store gallery callbacks */
    ui->gallery_active_cb = config ? config->gallery_active_cb : NULL;
    ui->gallery_ctx = config ? config->gallery_ctx : NULL;

    ui->tabview = lv_tabview_create(ui->main);
    lv_tabview_set_tab_bar_size(ui->tabview, 0);
    lv_obj_set_pos(ui->tabview, 0, 0);
    lv_obj_set_size(ui->tabview, BT_UI_WIDTH, BT_UI_HEIGHT);
    lv_obj_set_style_pad_all(ui->tabview, 0, 0);
    lv_obj_set_style_border_width(ui->tabview, 0, 0);

    /* Tab order: 0=Gallery(leftmost), 1=Music(default middle), 2=Dialer(rightmost) */
    lv_obj_t *tab_gallery = lv_tabview_add_tab(ui->tabview, "");
    lv_obj_t *tab_music   = lv_tabview_add_tab(ui->tabview, "");
    lv_obj_t *tab_dialer  = lv_tabview_add_tab(ui->tabview, "");
    lv_obj_clear_flag(tab_gallery, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(tab_music, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(tab_dialer, LV_OBJ_FLAG_SCROLLABLE);
    /* Remove tab padding so content fills edge-to-edge */
    lv_obj_set_style_pad_all(tab_gallery, 0, 0);
    lv_obj_set_style_pad_all(tab_music, 0, 0);
    lv_obj_set_style_pad_all(tab_dialer, 0, 0);
    lv_obj_set_style_border_width(tab_gallery, 0, 0);
    lv_obj_set_style_border_width(tab_music, 0, 0);
    lv_obj_set_style_border_width(tab_dialer, 0, 0);

    /* Enable smooth swipe transitions between tabs */
    lv_obj_t *tab_content = lv_tabview_get_content(ui->tabview);
    if (tab_content != NULL) {
        lv_obj_set_style_anim_duration(tab_content, 250, 0);
        lv_obj_set_style_pad_all(tab_content, 0, 0);
        lv_obj_set_style_border_width(tab_content, 0, 0);
    }

    ui->gallery = bt_ui_gallery_create(tab_gallery);
    ui->media = bt_ui_media_create(tab_music,
                                   config ? config->play_pause_cb : NULL, config ? config->play_pause_ctx : NULL,
                                   config ? config->prev_cb : NULL, config ? config->next_cb : NULL,
                                   config ? config->prev_next_ctx : NULL);
    /* Set back-pointer so cover decode can trigger global bg sync */
    if (ui->media) {
        bt_ui_media_refs_t *mrefs = (bt_ui_media_refs_t *)lv_obj_get_user_data(ui->media);
        if (mrefs) mrefs->ui_owner = ui;
    }
    ui->dialer = bt_ui_dialer_create(tab_dialer,
                                     config ? config->dial_cb : NULL, config ? config->dial_ctx : NULL,
                                     config ? config->end_call_cb : NULL, config ? config->end_call_ctx : NULL,
                                     config ? config->answer_call_cb : NULL, config ? config->answer_call_ctx : NULL);
    ui->volume_bar = bt_ui_volume_bar_create(ui->main);
    bt_ui_volume_bar_set_level(ui->volume_bar, ui->volume);

    /* Default to Music tab (tab 1) */
    lv_tabview_set_act(ui->tabview, BT_UI_TAB_MUSIC, LV_ANIM_OFF);

    /* Fire gallery_active_cb whenever user swipes to the gallery tab */
    lv_obj_add_event_cb(ui->tabview, tabview_value_changed_cb, LV_EVENT_VALUE_CHANGED, ui);

    lvgl_port_unlock();

    return ui;
}

void bt_ui_set_connected(bt_ui_t *ui, bool connected)
{
    if (ui == NULL || ui->splash == NULL || ui->main == NULL) {
        return;
    }
    lvgl_port_lock(0);
    if (connected) {
        lv_obj_add_flag(ui->splash, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui->main, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(ui->splash, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui->main, LV_OBJ_FLAG_HIDDEN);
    }
    lvgl_port_unlock();
}

void bt_ui_update_volume(bt_ui_t *ui, int volume)
{
    if (ui == NULL) {
        return;
    }
    ui->volume = volume < 0 ? 0 : (volume > 100 ? 100 : volume);
    if (ui->volume_bar == NULL) {
        return;
    }
    lvgl_port_lock(0);
    bt_ui_volume_bar_set_level(ui->volume_bar, ui->volume);
    lvgl_port_unlock();
}

int bt_ui_get_volume(const bt_ui_t *ui)
{
    return ui ? ui->volume : 0;
}

void bt_ui_update_playback_status(bt_ui_t *ui, uint32_t play_status)
{
    if (ui == NULL || ui->media == NULL) {
        return;
    }
    lvgl_port_lock(0);
    bt_ui_media_set_playing(ui->media, play_status == ESP_BT_AUDIO_PLAYBACK_STATUS_PLAYING);
    lvgl_port_unlock();
}

void bt_ui_update_track(bt_ui_t *ui, const char *title, const char *artist)
{
    if (ui == NULL || ui->media == NULL) {
        return;
    }
    lvgl_port_lock(0);
    bt_ui_media_set_track(ui->media, title, artist);
    lvgl_port_unlock();
}

void bt_ui_update_album(bt_ui_t *ui, const char *album)
{
    if (ui == NULL || ui->media == NULL) {
        return;
    }
    lvgl_port_lock(0);
    bt_ui_media_set_album(ui->media, album);
    lvgl_port_unlock();
}

void bt_ui_update_lyrics(bt_ui_t *ui, const char *lines[], int count)
{
    if (ui == NULL || ui->media == NULL) {
        return;
    }
    lvgl_port_lock(0);
    bt_ui_media_set_lyrics(ui->media, lines, count);
    lvgl_port_unlock();
}

static bt_ui_media_stream_type_t stream_type_from_profile(esp_bt_audio_stream_profile_t profile)
{
    switch (profile) {
        case ESP_BT_AUDIO_STREAM_PROFILE_LE_UNICAST:
            return BT_UI_MEDIA_STREAM_TYPE_CIS;
        case ESP_BT_AUDIO_STREAM_PROFILE_LE_BROADCAST:
            return BT_UI_MEDIA_STREAM_TYPE_BIS;
        default:
            return BT_UI_MEDIA_STREAM_TYPE_UNKNOWN;
    }
}

void bt_ui_update_stream_type(bt_ui_t *ui, esp_bt_audio_stream_handle_t stream,
                              esp_bt_audio_stream_state_t state)
{
    if (ui == NULL || ui->media == NULL || stream == NULL) {
        return;
    }
    if (state == ESP_BT_AUDIO_STREAM_STATE_STARTED) {
        esp_bt_audio_stream_profile_t profile = ESP_BT_AUDIO_STREAM_PROFILE_UNKNOWN;
        if (esp_bt_audio_stream_get_profile(stream, &profile) != ESP_OK) {
            return;
        }
        bt_ui_media_stream_type_t type = stream_type_from_profile(profile);
        if (type == BT_UI_MEDIA_STREAM_TYPE_UNKNOWN) {
            return;
        }
        ui->stream_type_stream = stream;
        ui->stream_type_is_broadcast = (type == BT_UI_MEDIA_STREAM_TYPE_BIS);
        lvgl_port_lock(0);
        bt_ui_media_set_stream_type(ui->media, type);
        if (ui->stream_type_is_broadcast) {
            bt_ui_media_set_track(ui->media, "正在收听广播", "");
        }
        lvgl_port_unlock();
    } else if ((state == ESP_BT_AUDIO_STREAM_STATE_STOPPED || state == ESP_BT_AUDIO_STREAM_STATE_RELEASED) &&
               ui->stream_type_stream == stream) {
        bool clear_broadcast_title = ui->stream_type_is_broadcast;
        ui->stream_type_stream = NULL;
        ui->stream_type_is_broadcast = false;
        lvgl_port_lock(0);
        bt_ui_media_set_stream_type(ui->media, BT_UI_MEDIA_STREAM_TYPE_UNKNOWN);
        if (clear_broadcast_title) {
            bt_ui_media_set_track(ui->media, "", "");
        }
        lvgl_port_unlock();
    }
}

void bt_ui_update_call_state(bt_ui_t *ui, int state, const char *number)
{
    if (ui == NULL || ui->dialer == NULL) {
        return;
    }
    lvgl_port_lock(0);
    bt_ui_dialer_set_call_state(ui->dialer, state, number);
    /* Switch to the dialer tab (tab 2) on any call activity; return to music (tab 1) when idle */
    if (ui->tabview != NULL) {
        uint32_t target_tab = (state != 0) ? BT_UI_TAB_DIALER : BT_UI_TAB_MUSIC;
        uint32_t cur_tab = lv_tabview_get_tab_active(ui->tabview);
        if (cur_tab != target_tab) {
            lv_tabview_set_act(ui->tabview, target_tab, LV_ANIM_ON);
        }
    }
    /* Fire gallery activation callback if user is on gallery tab */
    if (state == 0 && ui->gallery_active_cb != NULL) {
        uint32_t cur_tab = lv_tabview_get_tab_active(ui->tabview);
        if (cur_tab == BT_UI_TAB_GALLERY) {
            ui->gallery_active_cb(ui->gallery_ctx);
        }
    }
    lvgl_port_unlock();
}

void bt_ui_post_cover(bt_ui_t *ui, const uint8_t *data, size_t size)
{
    if (ui == NULL || ui->cover_queue == NULL) {
        return;
    }

    /* Clear request: post a NULL message to force cover removal */
    if (data == NULL || size == 0) {
        ui_cover_msg_t msg = { .data = NULL, .size = 0 };
        xQueueSend(ui->cover_queue, &msg, 0);
        return;
    }

    uint8_t *copy = heap_caps_malloc_prefer(size, 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT, MALLOC_CAP_DEFAULT);
    if (copy == NULL) {
        ESP_LOGW(TAG, "No memory for cover art copy, size %u", (unsigned)size);
        return;
    }
    memcpy(copy, data, size);
    ui_cover_msg_t msg = {
        .data = copy,
        .size = size,
    };
    if (xQueueSend(ui->cover_queue, &msg, 0) != pdTRUE) {
        free(copy);
        ESP_LOGW(TAG, "Cover art UI queue full, drop update");
    }
}

lv_obj_t *bt_ui_splash_create(lv_obj_t *parent, const char *device_name)
{
    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, BT_UI_WIDTH, BT_UI_HEIGHT);
    lv_obj_center(root);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(root, lv_color_hex(0x102840), 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *label = lv_label_create(root);
    lv_obj_set_style_text_color(label, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(label, BT_UI_FONT_LARGE, 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text_fmt(label, "Device Name:\n%s", device_name && device_name[0] ? device_name : "(BT)");
    lv_obj_center(label);
    return root;
}

static void btn_play_pause_click_cb(lv_event_t *e)
{
    lv_obj_t *btn = (lv_obj_t *)lv_event_get_target(e);
    lv_obj_t *root = lv_obj_get_parent(lv_obj_get_parent(btn));  /* btn -> btn_cont -> root */
    bt_ui_media_refs_t *refs = root ? (bt_ui_media_refs_t *)lv_obj_get_user_data(root) : NULL;
    if (refs && refs->play_pause_cb) {
        /* VALUE_CHANGED runs after toggle: checked => user requested play */
        bool want_play = lv_obj_has_state(btn, LV_STATE_CHECKED);
        refs->play_pause_cb(want_play, refs->play_pause_ctx);
    }
}

static void btn_prev_click_cb(lv_event_t *e)
{
    (void)e;
    lv_obj_t *btn = (lv_obj_t *)lv_event_get_target(e);
    lv_obj_t *root = lv_obj_get_parent(lv_obj_get_parent(btn));  /* btn -> btn_cont -> root */
    bt_ui_media_refs_t *refs = root ? (bt_ui_media_refs_t *)lv_obj_get_user_data(root) : NULL;
    if (refs && refs->prev_cb) {
        refs->prev_cb(refs->prev_next_ctx);
    }
}

static void btn_next_click_cb(lv_event_t *e)
{
    (void)e;
    lv_obj_t *btn = (lv_obj_t *)lv_event_get_target(e);
    lv_obj_t *root = lv_obj_get_parent(lv_obj_get_parent(btn));  /* btn -> btn_cont -> root */
    bt_ui_media_refs_t *refs = root ? (bt_ui_media_refs_t *)lv_obj_get_user_data(root) : NULL;
    if (refs && refs->next_cb) {
        refs->next_cb(refs->prev_next_ctx);
    }
}

/* Forward declaration */
static void cover_rotate_cb(void *var, int32_t v);

/**
 * @brief  Start or stop cover art rotation animation
 */
static void cover_set_spinning(lv_obj_t *cover_img, lv_anim_t *anim, bool spin)
{
    if (spin && !lv_anim_get(cover_img, cover_rotate_cb)) {
        lv_anim_init(anim);
        lv_anim_set_var(anim, cover_img);
        lv_anim_set_exec_cb(anim, cover_rotate_cb);
        lv_anim_set_values(anim, 0, 36000);
        lv_anim_set_duration(anim, 3600000);  /* 3600s/rotation (~1h), visually static */
        lv_anim_set_repeat_count(anim, LV_ANIM_REPEAT_INFINITE);
        lv_anim_set_path_cb(anim, lv_anim_path_linear);
        lv_anim_set_early_apply(anim, true);  /* Apply value before frame render */
        lv_anim_start(anim);
    } else if (!spin) {
        lv_anim_delete(cover_img, cover_rotate_cb);
    }
}

void bt_ui_media_set_playing(lv_obj_t *media_root, bool playing)
{
    if (media_root == NULL) {
        return;
    }
    bt_ui_media_refs_t *refs = (bt_ui_media_refs_t *)lv_obj_get_user_data(media_root);
    if (refs == NULL || refs->play_btn == NULL) {
        return;
    }
    /* Update play/pause button icon */
    lv_obj_t *lbl = lv_obj_get_child(refs->play_btn, 0);
    if (lbl != NULL && lv_obj_check_type(lbl, &lv_label_class)) {
        lv_label_set_text(lbl, playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
        if (playing) {
            lv_obj_add_state(refs->play_btn, LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(refs->play_btn, LV_STATE_CHECKED);
        }
    }
    /* Start/stop cover rotation */
    if (refs->cover_img != NULL) {
        cover_set_spinning(refs->cover_img, &refs->cover_anim, playing);
        refs->cover_spinning = playing;
    }
    /* Start/stop spectrum visualizer */
    if (refs->spectrum_timer != NULL) {
        if (playing) {
            lv_timer_resume(refs->spectrum_timer);
        } else {
            lv_timer_pause(refs->spectrum_timer);
        }
    }
}

void bt_ui_media_set_track(lv_obj_t *media_root, const char *title, const char *artist)
{
    if (media_root == NULL) {
        return;
    }
    bt_ui_media_refs_t *refs = (bt_ui_media_refs_t *)lv_obj_get_user_data(media_root);
    if (refs == NULL || refs->title_label == NULL) {
        return;
    }
    if (title != NULL) {
        lv_label_set_text(refs->title_label, title[0] != '\0' ? title : "Not Playing");
    }
    if (refs->artist_label != NULL && artist != NULL) {
        lv_label_set_text(refs->artist_label, artist[0] != '\0' ? artist : "");
    }
}

void bt_ui_media_set_album(lv_obj_t *media_root, const char *album)
{
    if (media_root == NULL) {
        return;
    }
    bt_ui_media_refs_t *refs = (bt_ui_media_refs_t *)lv_obj_get_user_data(media_root);
    if (refs != NULL && refs->album_label != NULL && album != NULL) {
        lv_label_set_text(refs->album_label, album[0] != '\0' ? album : "");
    }
}

void bt_ui_media_set_lyrics(lv_obj_t *media_root, const char *lines[], int count)
{
    if (media_root == NULL) {
        return;
    }
    bt_ui_media_refs_t *refs = (bt_ui_media_refs_t *)lv_obj_get_user_data(media_root);
    if (refs == NULL || refs->lyric_cont == NULL) {
        return;
    }
    for (int i = 0; i < 5; i++) {
        if (refs->lyric_lines[i] != NULL) {
            const char *text = "";
            if (i < count && lines && lines[i]) {
                text = lines[i];
            }
            /* Middle line (index 2) is the active highlighted line */
            bool is_active = (i == 2 && count > 0);
            lv_label_set_text(refs->lyric_lines[i], text);
            lv_label_set_long_mode(refs->lyric_lines[i], LV_LABEL_LONG_WRAP);
            lv_obj_set_style_text_color(refs->lyric_lines[i],
                lv_color_hex(is_active ? 0x1ed760 : 0x8899aa), 0);
            lv_obj_set_style_text_font(refs->lyric_lines[i],
                is_active ? BT_UI_FONT_MEDIUM : BT_UI_FONT_SMALL, 0);
            lv_obj_set_style_text_opa(refs->lyric_lines[i],
                is_active ? LV_OPA_COVER : LV_OPA_50, 0);
        }
    }
}

void bt_ui_media_set_bg_color(lv_obj_t *media_root, lv_color_t color)
{
    if (media_root == NULL) {
        return;
    }
    bt_ui_media_refs_t *refs = (bt_ui_media_refs_t *)lv_obj_get_user_data(media_root);
    if (refs != NULL) {
        refs->bg_color = color;
        lv_obj_set_style_bg_color(media_root, color, 0);
    }
}

void bt_ui_media_set_stream_type(lv_obj_t *media_root, bt_ui_media_stream_type_t type)
{
    /* Stream type icons not used in vinyl layout - simplified */
    (void)media_root;
    (void)type;
}

/* ====== Audio Spectrum Visualizer ====== */

/* Spectrum bar colors: rainbow palette distributed across bar indices, brightness by height */
static lv_color_t spectrum_bar_color(int bar_idx, int height, int max_h)
{
    float t = (float)height / (float)max_h;
    /* 16 bars = 16 hue stops around a color wheel, each bar a different base color */
    float hue = (float)bar_idx * 22.5f;  /* 360°/16 = 22.5° per bar */
    float s = 0.8f + t * 0.2f;  /* Medium to full saturation */
    float v = 0.4f + t * 0.6f;  /* Dim at bottom, bright at top */

    /* HSV to RGB */
    float c = v * s;
    float x = c * (1.0f - fabsf(fmodf(hue / 60.0f, 2.0f) - 1.0f));
    float m = v - c;
    float r, g, b;
    if      (hue < 60.0f)  { r = c; g = x; b = 0; }
    else if (hue < 120.0f) { r = x; g = c; b = 0; }
    else if (hue < 180.0f) { r = 0; g = c; b = x; }
    else if (hue < 240.0f) { r = 0; g = x; b = c; }
    else if (hue < 300.0f) { r = x; g = 0; b = c; }
    else                   { r = c; g = 0; b = x; }
    int ir = (int)((r + m) * 255.0f);
    int ig = (int)((g + m) * 255.0f);
    int ib = (int)((b + m) * 255.0f);
    if (ir > 255) ir = 255;
    if (ir < 0) ir = 0;
    if (ig > 255) ig = 255;
    if (ig < 0) ig = 0;
    if (ib > 255) ib = 255;
    if (ib < 0) ib = 0;
    return lv_color_make((uint8_t)ir, (uint8_t)ig, (uint8_t)ib);
}

/** Spectrum timer callback: random-walk bar heights for fluid animation */
static void spectrum_timer_cb(lv_timer_t *timer)
{
    bt_ui_media_refs_t *refs = (bt_ui_media_refs_t *)lv_timer_get_user_data(timer);
    if (refs == NULL || refs->spectrum_cont == NULL) return;

    int max_h = SPECTRUM_H - 4;

    for (int i = 0; i < SPECTRUM_BAR_COUNT; i++) {
        /* Smooth random walk: drift toward target with some randomness */
        int target = 0;
        /* Bias: low-mid frequencies bigger, high frequencies smaller */
        if (i < 4)          target = (esp_random() % (max_h * 3 / 4)) + max_h / 4;  /* bass */
        else if (i < 8)    target = (esp_random() % (max_h * 2 / 3)) + max_h / 6;  /* low-mid */
        else if (i < 12)   target = (esp_random() % (max_h / 2)) + max_h / 8;      /* mid */
        else                target = esp_random() % (max_h / 3);                     /* high */

        /* Smooth approach: move 30% toward target */
        int cur = refs->spectrum_heights[i];
        int delta = target - cur;
        refs->spectrum_heights[i] = (int8_t)(cur + delta * 3 / 10);
        if (refs->spectrum_heights[i] < 2) refs->spectrum_heights[i] = 2;
        if (refs->spectrum_heights[i] > max_h) refs->spectrum_heights[i] = (int8_t)max_h;
    }

    lv_obj_invalidate(refs->spectrum_cont);
}

/** Spectrum draw event: paints vertical bars */
static void spectrum_draw_cb(lv_event_t *e)
{
    lv_obj_t *cont = lv_event_get_target(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    if (cont == NULL || layer == NULL) return;

    lv_area_t coords;
    lv_obj_get_coords(cont, &coords);
    int32_t base_y = coords.y2;
    int32_t max_h = coords.y2 - coords.y1;

    bt_ui_media_refs_t *refs = (bt_ui_media_refs_t *)lv_obj_get_user_data(cont);
    if (refs == NULL) return;

    for (int i = 0; i < SPECTRUM_BAR_COUNT; i++) {
        int h = refs->spectrum_heights[i];
        if (h < 2) h = 2;
        int bar_left = coords.x1 + i * (SPECTRUM_W / SPECTRUM_BAR_COUNT) + 1;

        lv_area_t bar_area;
        bar_area.x1 = (int32_t)bar_left;
        bar_area.y1 = (int32_t)(base_y - h);
        bar_area.x2 = (int32_t)(bar_left + SPECTRUM_BAR_W - 1);
        bar_area.y2 = (int32_t)base_y;

        lv_draw_rect_dsc_t rect_dsc;
        lv_draw_rect_dsc_init(&rect_dsc);
        lv_color_t bar_color = spectrum_bar_color(i, h, max_h);
        rect_dsc.bg_color = bar_color;
        rect_dsc.bg_opa = (lv_opa_t)(200 + h * 55 / max_h);
        rect_dsc.radius = 3;
        lv_draw_rect(layer, &rect_dsc, &bar_area);
    }
}

/** Create and attach spectrum visualizer to the media page */
static void media_spectrum_create(bt_ui_media_refs_t *refs)
{
    if (refs == NULL) return;

    lv_obj_t *root = lv_obj_get_parent(refs->cover_clip);  /* root is parent of cover_clip */
    if (root == NULL) return;

    lv_obj_t *cont = lv_obj_create(root);
    lv_obj_remove_style_all(cont);
    lv_obj_set_size(cont, SPECTRUM_W, SPECTRUM_H);
    lv_obj_set_pos(cont, SPECTRUM_X, BT_UI_HEIGHT - SPECTRUM_H - 8);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_all(cont, 0, 0);
    lv_obj_add_event_cb(cont, spectrum_draw_cb, LV_EVENT_DRAW_MAIN, NULL);
    lv_obj_set_user_data(cont, refs);  /* Back-pointer for draw cb */

    refs->spectrum_cont = cont;
    memset(refs->spectrum_heights, 0, sizeof(refs->spectrum_heights));

    /* Start animation timer — only when playing */
    refs->spectrum_timer = lv_timer_create(spectrum_timer_cb, SPECTRUM_UPDATE_MS, refs);
    lv_timer_pause(refs->spectrum_timer);  /* Start paused, resume on play */
}

/* ====== End Spectrum Visualizer ====== */

void bt_ui_media_set_cover_image(lv_obj_t *media_root, const uint8_t *data, size_t size)
{
    ESP_LOGD(TAG, "Set cover image: media_root=%p data=%p size=%u", (void *)media_root, (void *)data,
             (unsigned)size);
    if (media_root == NULL) {
        ESP_LOGW(TAG, "Set cover image: media_root is NULL");
        return;
    }
    bt_ui_media_refs_t *refs = (bt_ui_media_refs_t *)lv_obj_get_user_data(media_root);
    if (refs == NULL || refs->cover_img == NULL) {
        ESP_LOGW(TAG, "Set cover image: refs=%p or cover_img is NULL", (void *)refs);
        return;
    }
    if (data == NULL || size == 0) {
        ESP_LOGD(TAG, "Set cover image: clear cover");
        lv_obj_add_flag(refs->cover_img, LV_OBJ_FLAG_HIDDEN);
        if (refs->cover_data != NULL) {
            free(refs->cover_data);
            refs->cover_data = NULL;
            refs->cover_size = 0;
        }
        return;
    }

    /* Immediately hide old cover while new one decodes (sync, in LVGL lock) */
    lv_obj_add_flag(refs->cover_img, LV_OBJ_FLAG_HIDDEN);

    esp_jpeg_image_cfg_t cfg = {
        .indata = (uint8_t *)data,
        .indata_size = (uint32_t)size,
        .outbuf = NULL,
        .outbuf_size = 0,
        .out_format = JPEG_IMAGE_FORMAT_RGB565,
        .out_scale = JPEG_IMAGE_SCALE_0,
        .flags = {.swap_color_bytes = 0},  /* No swap; LCD big-endian handled by LVGL port driver */
        .advanced = {.working_buffer = NULL, .working_buffer_size = 0},
        .priv = {0},
    };
    esp_jpeg_image_output_t info;
    esp_err_t err = esp_jpeg_get_image_info(&cfg, &info);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Set cover image failed: esp_jpeg_get_image_info scale 1/1 failed, %s",
                 esp_err_to_name(err));
        free((void *)data);
        return;
    }
    uint32_t src_w = (uint32_t)info.width;
    uint32_t src_h = (uint32_t)info.height;
    ESP_LOGD(TAG, "Set cover image: jpeg info %" PRIu32 "x%" PRIu32 " output_len=%u", src_w, src_h,
             (unsigned)info.output_len);
    int target_sz = COVER_SIZE;
    esp_jpeg_image_scale_t scale = JPEG_IMAGE_SCALE_0;
    if (src_w > target_sz || src_h > target_sz) {
        if (src_w / 2 <= target_sz && src_h / 2 <= target_sz) {
            scale = JPEG_IMAGE_SCALE_1_2;
        } else if (src_w / 4 <= target_sz && src_h / 4 <= target_sz) {
            scale = JPEG_IMAGE_SCALE_1_4;
        } else {
            scale = JPEG_IMAGE_SCALE_1_8;
        }
    }
    cfg.out_scale = scale;
    if (esp_jpeg_get_image_info(&cfg, &info) != ESP_OK) {
        ESP_LOGE(TAG, "Set cover image failed: esp_jpeg_get_image_info scale %d failed", (int)scale);
        free((void *)data);
        return;
    }
    ESP_LOGD(TAG, "Set cover image: decode scale=%d out %ux%u output_len=%u", (int)scale,
             (unsigned)info.width, (unsigned)info.height, (unsigned)info.output_len);

    uint8_t *outbuf = (uint8_t *)malloc(info.output_len);
    if (outbuf == NULL) {
        ESP_LOGE(TAG, "Set cover image failed: malloc(%u) failed", (unsigned)info.output_len);
        free((void *)data);
        return;
    }
    cfg.outbuf = outbuf;
    cfg.outbuf_size = (uint32_t)info.output_len;
    esp_jpeg_image_output_t outimg;
    err = esp_jpeg_decode(&cfg, &outimg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Set cover image failed: esp_jpeg_decode failed, %s", esp_err_to_name(err));
        free(outbuf);
        free((void *)data);
        return;
    }
    ESP_LOGD(TAG, "Set cover image: decoded %ux%u output_len=%u", (unsigned)outimg.width,
             (unsigned)outimg.height, (unsigned)outimg.output_len);
    free((void *)data);

    /* ====== Apply circular mask: set corner pixels to background color ====== */
    {
        int32_t im_w = (int32_t)outimg.width;
        int32_t im_h = (int32_t)outimg.height;
        int32_t cx = im_w / 2;
        int32_t cy = im_h / 2;
        int32_t r = (cx < cy) ? cx : cy;  /* min radius */
        int32_t r2 = r * r;
        /* Background: lv_color_hex(0x102840) in native RGB565 little-endian:
         *   R=0x10(16/255) G=0x28(40/255) B=0x40(64/255)
         *   → r5=2 g6=10 b5=8 → 16-bit=0x1148 → bytes [0x48, 0x11] */
        uint8_t bg_lo = 0x48;
        uint8_t bg_hi = 0x11;
        uint8_t *px = outbuf;
        for (int32_t y = 0; y < im_h; y++) {
            int32_t dy = y - cy;
            int32_t dy2 = dy * dy;
            for (int32_t x = 0; x < im_w; x++) {
                int32_t dx = x - cx;
                if (dx * dx + dy2 > r2) {
                    px[0] = bg_lo;
                    px[1] = bg_hi;
                }
                px += 2;
            }
        }
    }
    /* ====== End circular mask ====== */

    if (refs->cover_data != NULL) {
        free(refs->cover_data);
    }
    refs->cover_data = outbuf;
    refs->cover_size = outimg.output_len;

    if (refs->cover_img != NULL) {
        uint32_t stride = (uint32_t)outimg.width * 2;  /* RGB565 = 2 bytes per pixel */
#if LVGL_VERSION_MAJOR >= 9
        memset(&refs->cover_dsc, 0, sizeof(refs->cover_dsc));
        refs->cover_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
        refs->cover_dsc.header.cf = LV_COLOR_FORMAT_RGB565;
        refs->cover_dsc.header.flags = 0;
        refs->cover_dsc.header.w = (int32_t)outimg.width;
        refs->cover_dsc.header.h = (int32_t)outimg.height;
        refs->cover_dsc.header.stride = (uint32_t)stride;
        refs->cover_dsc.data_size = (uint32_t)outimg.output_len;
        refs->cover_dsc.data = refs->cover_data;
        lv_image_set_src(refs->cover_img, &refs->cover_dsc);
#else
        memset(&refs->cover_img_dsc, 0, sizeof(refs->cover_img_dsc));
        refs->cover_img_dsc.header.cf = LV_IMG_CF_TRUE_COLOR;
        refs->cover_img_dsc.header.w = (int32_t)outimg.width;
        refs->cover_img_dsc.header.h = (int32_t)outimg.height;
        refs->cover_img_dsc.header.stride = (uint32_t)stride;
        refs->cover_img_dsc.data_size = (uint32_t)outimg.output_len;
        refs->cover_img_dsc.data = refs->cover_data;
        lv_img_set_src(refs->cover_img, &refs->cover_img_dsc);
#endif  /* LVGL_VERSION_MAJOR >= 9 */
        lv_obj_clear_flag(refs->cover_img, LV_OBJ_FLAG_HIDDEN);

        /* Extract dominant color for background */
        uint32_t r_sum = 0, g_sum = 0, b_sum = 0;
        int sample_count = 0;
        uint8_t *pixels = refs->cover_data;
        for (int i = 0; i < (int)(outimg.width * outimg.height); i += 16) {
            uint16_t px = ((uint16_t)pixels[i * 2 + 1] << 8) | pixels[i * 2];
            r_sum += (px >> 11) & 0x1F;
            g_sum += (px >> 5) & 0x3F;
            b_sum += px & 0x1F;
            sample_count++;
        }
        if (sample_count > 0) {
            uint8_t r = ((r_sum / sample_count) * 255 / 31);
            uint8_t g = ((g_sum / sample_count) * 255 / 63);
            uint8_t b = ((b_sum / sample_count) * 255 / 31);
            lv_color_t full_color = lv_color_make(r, g, b);
            /* Darken the color for background (25% brightness) */
            r = r * 25 / 100;
            g = g * 25 / 100;
            b = b * 25 / 100;
            lv_color_t bg = lv_color_make(r, g, b);
            bt_ui_media_set_bg_color(media_root, bg);
            /* Propagate to all pages via ui_owner back-pointer */
            if (refs->ui_owner != NULL) {
                refs->ui_owner->global_bg_color = full_color;
                bt_ui_sync_global_bg(refs->ui_owner);
            }
        }
    }
    ESP_LOGD(TAG, "Set cover image: done, cover visible %ux%u", (unsigned)outimg.width,
             (unsigned)outimg.height);
}

/** Propagate background color to all UI pages */
static void bt_ui_sync_global_bg(bt_ui_t *ui)
{
    if (ui == NULL) return;
    lvgl_port_lock(0);

    /* Darken the global color to 25% for background (direct RGB565 arithmetic) */
    lv_color_t bg_dark;
    bg_dark.red   = (uint8_t)((uint16_t)ui->global_bg_color.red * 25 / 100);
    bg_dark.green = (uint8_t)((uint16_t)ui->global_bg_color.green * 25 / 100);
    bg_dark.blue  = (uint8_t)((uint16_t)ui->global_bg_color.blue * 25 / 100);

    /* Update the active screen background */
    lv_obj_t *scr = lv_scr_act();
    if (scr) lv_obj_set_style_bg_color(scr, bg_dark, 0);

    /* Update main container */
    if (ui->main) lv_obj_set_style_bg_color(ui->main, bg_dark, 0);

    /* Update media page */
    if (ui->media) {
        bt_ui_media_set_bg_color(ui->media, bg_dark);
    }

    /* Update gallery page */
    if (ui->gallery) lv_obj_set_style_bg_color(ui->gallery, bg_dark, 0);

    /* Update dialer page */
    if (ui->dialer) lv_obj_set_style_bg_color(ui->dialer, bg_dark, 0);

    /* Update splash page */
    if (ui->splash) lv_obj_set_style_bg_color(ui->splash, bg_dark, 0);

    lvgl_port_unlock();
}

/**
 * @brief  Cover art rotation animation callback
 */
static void cover_rotate_cb(void *var, int32_t v)
{
    lv_obj_t *obj = (lv_obj_t *)var;
    lv_obj_set_style_transform_rotation(obj, v, 0);
}

/**
 * @brief  Create the media player page with a circular spinning cover art.
 */
lv_obj_t *bt_ui_media_create(lv_obj_t *parent,
                             void (*play_pause_cb)(bool want_play, void *ctx), void *play_pause_ctx,
                             void (*prev_cb)(void *ctx), void (*next_cb)(void *ctx), void *prev_next_ctx)
{
    /* Root container */
    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, BT_UI_WIDTH, CONTENT_H);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(root, lv_color_hex(0x102840), 0);

    /* ====== LEFT: Circular Cover Clip Container + Spinning Image ====== */
    lv_obj_t *cover_clip = lv_obj_create(root);
    lv_obj_remove_style_all(cover_clip);
    lv_obj_set_size(cover_clip, COVER_SIZE, COVER_SIZE);
    lv_obj_set_pos(cover_clip, COVER_X, COVER_Y);
    lv_obj_clear_flag(cover_clip, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(cover_clip, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_clip_corner(cover_clip, true, 0);
    lv_obj_set_style_bg_opa(cover_clip, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cover_clip, 0, 0);
    lv_obj_set_style_pad_all(cover_clip, 0, 0);

    /* Cover image INSIDE the clip container — spins while clip keeps it circular */
#if LVGL_VERSION_MAJOR >= 9
    lv_obj_t *cover_img = lv_image_create(cover_clip);
#else
    lv_obj_t *cover_img = lv_img_create(cover_clip);
#endif
    lv_obj_set_size(cover_img, COVER_SIZE, COVER_SIZE);
    lv_obj_set_pos(cover_img, 0, 0);
    /* Also set radius on cover image so its corners are clipped when rotated */
    lv_obj_set_style_radius(cover_img, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_clip_corner(cover_img, true, 0);
    /* CRITICAL: Set pivot point to object center for self-rotation (自转, not 公转) */
    lv_obj_set_style_transform_pivot_x(cover_img, COVER_SIZE / 2, 0);
    lv_obj_set_style_transform_pivot_y(cover_img, COVER_SIZE / 2, 0);
    lv_obj_add_flag(cover_img, LV_OBJ_FLAG_HIDDEN);

    /* ====== RIGHT: Track Info Panel ====== */
    lv_obj_t *right_panel = lv_obj_create(root);
    lv_obj_remove_style_all(right_panel);
    lv_obj_set_size(right_panel, RIGHT_PANEL_W, CONTENT_H - 16);
    lv_obj_set_pos(right_panel, RIGHT_PANEL_X, 8);
    lv_obj_clear_flag(right_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(right_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(right_panel, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(right_panel, 8, 0);
    lv_obj_set_style_pad_row(right_panel, 12, 0);

    /* Title (large, white, centered) */
    lv_obj_t *title_label = lv_label_create(right_panel);
    lv_label_set_text(title_label, "Not Playing");
    lv_label_set_long_mode(title_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(title_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(title_label, lv_color_hex(0xffffff), 0);
    lv_obj_set_width(title_label, RIGHT_PANEL_W - 24);
    lv_obj_set_style_text_font(title_label, BT_UI_FONT_LARGE, 0);

    /* Artist (medium, grey) */
    lv_obj_t *artist_label = lv_label_create(right_panel);
    lv_label_set_text(artist_label, "");
    lv_label_set_long_mode(artist_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(artist_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(artist_label, lv_color_hex(0x99aabb), 0);
    lv_obj_set_width(artist_label, RIGHT_PANEL_W - 24);
    lv_obj_set_style_text_font(artist_label, BT_UI_FONT_MEDIUM, 0);

    /* Album (small, dim) */
    lv_obj_t *album_label = lv_label_create(right_panel);
    lv_label_set_text(album_label, "");
    lv_label_set_long_mode(album_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(album_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(album_label, lv_color_hex(0x778899), 0);
    lv_obj_set_width(album_label, RIGHT_PANEL_W - 24);
    lv_obj_set_style_text_font(album_label, BT_UI_FONT_SMALL, 0);

    /* Lyrics area (kept for future use, hidden by default) */
    lv_obj_t *lyric_cont = lv_obj_create(right_panel);
    lv_obj_remove_style_all(lyric_cont);
    lv_obj_set_size(lyric_cont, 0, 0);  /* Hidden — no space */
    lv_obj_add_flag(lyric_cont, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(lyric_cont, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < 5; i++) {
        lv_obj_t *lyric_line = lv_label_create(lyric_cont);
        lv_label_set_text(lyric_line, "");
        lv_label_set_long_mode(lyric_line, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(lyric_line, LV_TEXT_ALIGN_LEFT, 0);
        lv_obj_set_style_text_color(lyric_line, lv_color_hex(i == 2 ? 0x1ed760 : 0x8899aa), 0);
        lv_obj_set_style_text_opa(lyric_line, i == 2 ? LV_OPA_COVER : LV_OPA_50, 0);
        lv_obj_set_width(lyric_line, RIGHT_PANEL_W - 16);
        lv_obj_set_pos(lyric_line, 8, i * 56 + 8);
        lv_obj_set_style_text_font(lyric_line,
            i == 2 ? BT_UI_FONT_MEDIUM : BT_UI_FONT_SMALL, 0);
    }

    /* ====== BOTTOM-RIGHT: Control buttons ====== */
    lv_obj_t *btn_cont = lv_obj_create(root);
    lv_obj_remove_style_all(btn_cont);
    lv_obj_set_size(btn_cont, 260, BTN_ROW_H);
    lv_obj_set_pos(btn_cont, BT_UI_WIDTH - 260 - 8, BT_UI_HEIGHT - BTN_ROW_H - 12);
    lv_obj_set_flex_flow(btn_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(btn_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_row(btn_cont, 24, 0);

    /* Prev button */
#if LVGL_VERSION_MAJOR >= 9
    lv_obj_t *btn_prev = lv_button_create(btn_cont);
#else
    lv_obj_t *btn_prev = lv_btn_create(btn_cont);
#endif
    lv_obj_set_size(btn_prev, BTN_W, BTN_H);
    lv_obj_set_style_radius(btn_prev, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(btn_prev, lv_color_hex(0x334455), 0);
    lv_obj_set_style_bg_opa(btn_prev, LV_OPA_50, 0);
    lv_obj_set_style_shadow_width(btn_prev, 0, 0);
    lv_obj_t *lbl_prev = lv_label_create(btn_prev);
    lv_label_set_text(lbl_prev, LV_SYMBOL_PREV);
    lv_obj_set_style_text_font(lbl_prev, BT_UI_FONT_ICON, 0);
    lv_obj_set_style_text_color(lbl_prev, lv_color_hex(0xcccccc), 0);
    lv_obj_center(lbl_prev);
    lv_obj_add_event_cb(btn_prev, btn_prev_click_cb, LV_EVENT_CLICKED, NULL);

    /* Play/Pause button */
#if LVGL_VERSION_MAJOR >= 9
    lv_obj_t *btn_play = lv_button_create(btn_cont);
#else
    lv_obj_t *btn_play = lv_btn_create(btn_cont);
#endif
    lv_obj_set_size(btn_play, BTN_W + 16, BTN_H + 8);
    lv_obj_set_style_radius(btn_play, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(btn_play, lv_color_hex(0x00cc66), 0);
    lv_obj_set_style_bg_opa(btn_play, LV_OPA_80, 0);
    lv_obj_set_style_shadow_width(btn_play, 0, 0);
    lv_obj_add_flag(btn_play, LV_OBJ_FLAG_CHECKABLE);
    lv_obj_t *lbl_play = lv_label_create(btn_play);
    lv_label_set_text(lbl_play, LV_SYMBOL_PLAY);
    lv_obj_set_style_text_font(lbl_play, BT_UI_FONT_ICON, 0);
    lv_obj_set_style_text_color(lbl_play, lv_color_hex(0xffffff), 0);
    lv_obj_center(lbl_play);
    lv_obj_add_event_cb(btn_play, btn_play_pause_click_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* Next button */
#if LVGL_VERSION_MAJOR >= 9
    lv_obj_t *btn_next = lv_button_create(btn_cont);
#else
    lv_obj_t *btn_next = lv_btn_create(btn_cont);
#endif
    lv_obj_set_size(btn_next, BTN_W, BTN_H);
    lv_obj_set_style_radius(btn_next, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(btn_next, lv_color_hex(0x334455), 0);
    lv_obj_set_style_bg_opa(btn_next, LV_OPA_50, 0);
    lv_obj_set_style_shadow_width(btn_next, 0, 0);
    lv_obj_t *lbl_next = lv_label_create(btn_next);
    lv_label_set_text(lbl_next, LV_SYMBOL_NEXT);
    lv_obj_set_style_text_font(lbl_next, BT_UI_FONT_ICON, 0);
    lv_obj_set_style_text_color(lbl_next, lv_color_hex(0xcccccc), 0);
    lv_obj_center(lbl_next);
    lv_obj_add_event_cb(btn_next, btn_next_click_cb, LV_EVENT_CLICKED, NULL);

    /* Media refs */
    static bt_ui_media_refs_t s_media_refs;
    memset(&s_media_refs, 0, sizeof(s_media_refs));
    s_media_refs.play_btn = btn_play;
    s_media_refs.title_label = title_label;
    s_media_refs.artist_label = artist_label;
    s_media_refs.album_label = album_label;
    s_media_refs.cover_clip = cover_clip;
    s_media_refs.cover_img = cover_img;
    s_media_refs.cover_spinning = false;
    s_media_refs.lyric_cont = lyric_cont;
    s_media_refs.lyric_timer = NULL;
    for (int i = 0; i < 5; i++) {
        s_media_refs.lyric_lines[i] = lv_obj_get_child(lyric_cont, i);
    }
    s_media_refs.cover_data = NULL;
    s_media_refs.cover_size = 0;
    s_media_refs.play_pause_cb = play_pause_cb;
    s_media_refs.play_pause_ctx = play_pause_ctx;
    s_media_refs.prev_cb = prev_cb;
    s_media_refs.next_cb = next_cb;
    s_media_refs.prev_next_ctx = prev_next_ctx;
    s_media_refs.bg_color = lv_color_hex(0x102840);
    lv_obj_set_user_data(root, &s_media_refs);

    /* Create spectrum visualizer at bottom-left */
    media_spectrum_create(&s_media_refs);

    return root;
}

static void dialer_digit_click_cb(lv_event_t *e)
{
    lv_obj_t *btn = (lv_obj_t *)lv_event_get_target(e);
    lv_obj_t *lbl = (lv_obj_t *)lv_obj_get_child(btn, 0);
    const char *txt = lbl ? lv_label_get_text(lbl) : NULL;
    if (txt == NULL || dialer_number_label == NULL) {
        return;
    }
    size_t len = lv_strlen(dialer_number_buf);
    if (len + 2 >= DIALER_BUF_SIZE) {
        return;
    }
    if (txt[0] != '\0') {
        dialer_number_buf[len] = txt[0];
        dialer_number_buf[len + 1] = '\0';
    }
    lv_label_set_text(dialer_number_label, dialer_number_buf[0] ? dialer_number_buf : "Enter Number");
}

static void dialer_back_click_cb(lv_event_t *e)
{
    (void)e;
    if (dialer_number_label == NULL) {
        return;
    }
    size_t len = lv_strlen(dialer_number_buf);
    if (len > 0) {
        dialer_number_buf[len - 1] = '\0';
    }
    lv_label_set_text(dialer_number_label, dialer_number_buf[0] ? dialer_number_buf : "Enter Number");
}

static void dialer_call_click_cb(lv_event_t *e)
{
    lv_obj_t *btn = (lv_obj_t *)lv_event_get_target(e);
    dialer_call_ctx_t *ctx = (dialer_call_ctx_t *)lv_obj_get_user_data(btn);
    if (ctx == NULL) {
        return;
    }
    if (ctx->in_call) {
        if (ctx->end_call_cb) {
            ctx->end_call_cb(ctx->end_call_cb_ctx);
        }
        return;
    }
    if (ctx->call_cb) {
        ctx->call_cb(dialer_number_buf, ctx->call_cb_ctx);
    }
}

static void dialer_answer_click_cb(lv_event_t *e)
{
    lv_obj_t *btn = (lv_obj_t *)lv_event_get_target(e);
    dialer_call_ctx_t *ctx = (dialer_call_ctx_t *)lv_obj_get_user_data(btn);
    if (ctx && ctx->answer_call_cb) {
        ctx->answer_call_cb(ctx->answer_call_cb_ctx);
    }
}

lv_obj_t *bt_ui_dialer_create(lv_obj_t *parent,
                              void (*call_cb)(const char *number, void *ctx), void *call_cb_ctx,
                              void (*end_call_cb)(void *ctx), void *end_call_cb_ctx,
                              void (*answer_call_cb)(void *ctx), void *answer_call_cb_ctx)
{
    dialer_number_buf[0] = '\0';
    dialer_number_label = NULL;

    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, BT_UI_WIDTH, CONTENT_H);
    lv_obj_center(root);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(root, lv_color_hex(0x102840), 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    dialer_number_label = lv_label_create(root);
    lv_label_set_text(dialer_number_label, "");
    lv_obj_set_style_text_color(dialer_number_label, lv_color_hex(0x00ff88), 0);
    lv_obj_set_width(dialer_number_label, BT_UI_WIDTH / 2 - 32);
    lv_label_set_long_mode(dialer_number_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(dialer_number_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(dialer_number_label, LV_ALIGN_TOP_RIGHT, -24, 70);
    lv_obj_set_style_text_font(dialer_number_label, &lv_font_montserrat_32, 0);

    static const char *keys[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "*", "0", "#"};
    const int cols = 3, rows = 4;
    const int key_sz = DIALER_KEYPAD_SZ;
    int start_x = 46;
    int start_y = (CONTENT_H - (rows * key_sz + (rows - 1) * DIALER_KEY_GAP)) / 2;
    const lv_color_t key_gray = lv_color_hex(0x6a6a6a);

    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            int i = r * cols + c;
#if LVGL_VERSION_MAJOR >= 9
            lv_obj_t *btn = lv_button_create(root);
#else
            lv_obj_t *btn = lv_btn_create(root);
#endif  /* LVGL_VERSION_MAJOR >= 9 */
            lv_obj_set_size(btn, key_sz, key_sz);
            lv_obj_set_style_shadow_width(btn, 0, 0);
            lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(btn, key_gray, 0);
            lv_obj_set_pos(btn, start_x + c * (key_sz + DIALER_KEY_GAP), start_y + r * (key_sz + DIALER_KEY_GAP));
            lv_obj_t *lbl = lv_label_create(btn);
            lv_label_set_text(lbl, keys[i]);
            lv_obj_center(lbl);
            lv_obj_set_style_text_color(lbl, lv_color_hex(0xffffff), 0);
            lv_obj_set_style_text_font(lbl, &lv_font_montserrat_28, 0);
            lv_obj_add_event_cb(btn, dialer_digit_click_cb, LV_EVENT_CLICKED, NULL);
        }
    }

#if LVGL_VERSION_MAJOR >= 9
    lv_obj_t *call_btn = lv_button_create(root);
#else
    lv_obj_t *call_btn = lv_btn_create(root);
#endif  /* LVGL_VERSION_MAJOR >= 9 */
    lv_obj_set_size(call_btn, key_sz, key_sz);
    lv_obj_set_style_shadow_width(call_btn, 0, 0);
    lv_obj_set_style_radius(call_btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(call_btn, lv_color_hex(0x2E7D32), 0);
    lv_obj_set_pos(call_btn, BT_UI_WIDTH - 284, CONTENT_H - key_sz - 54);
    static dialer_call_ctx_t s_dialer_call_ctx;
    s_dialer_call_ctx.call_cb = call_cb;
    s_dialer_call_ctx.call_cb_ctx = call_cb_ctx;
    s_dialer_call_ctx.end_call_cb = end_call_cb;
    s_dialer_call_ctx.end_call_cb_ctx = end_call_cb_ctx;
    s_dialer_call_ctx.answer_call_cb = answer_call_cb;
    s_dialer_call_ctx.answer_call_cb_ctx = answer_call_cb_ctx;
    s_dialer_call_ctx.in_call = false;
    lv_obj_set_user_data(call_btn, &s_dialer_call_ctx);
    lv_obj_add_event_cb(call_btn, dialer_call_click_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *call_lbl = lv_label_create(call_btn);
    lv_label_set_text(call_lbl, LV_SYMBOL_CALL);
    lv_obj_center(call_lbl);
    lv_obj_set_style_text_color(call_lbl, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(call_lbl, BT_UI_FONT_ICON, 0);

#if LVGL_VERSION_MAJOR >= 9
    lv_obj_t *back_btn = lv_button_create(root);
#else
    lv_obj_t *back_btn = lv_btn_create(root);
#endif  /* LVGL_VERSION_MAJOR >= 9 */
    lv_obj_set_size(back_btn, key_sz, key_sz);
    lv_obj_set_style_shadow_width(back_btn, 0, 0);
    lv_obj_set_style_radius(back_btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x795548), 0);
    lv_obj_set_pos(back_btn, BT_UI_WIDTH - 158, CONTENT_H - key_sz - 54);
    lv_obj_t *back_lbl = lv_label_create(back_btn);
    lv_label_set_text(back_lbl, LV_SYMBOL_BACKSPACE);
    lv_obj_center(back_lbl);
    lv_obj_set_style_text_color(back_lbl, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(back_lbl, BT_UI_FONT_ICON, 0);
    lv_obj_add_event_cb(back_btn, dialer_back_click_cb, LV_EVENT_CLICKED, NULL);

#if LVGL_VERSION_MAJOR >= 9
    lv_obj_t *answer_btn = lv_button_create(root);
#else
    lv_obj_t *answer_btn = lv_btn_create(root);
#endif  /* LVGL_VERSION_MAJOR >= 9 */
    lv_obj_set_size(answer_btn, key_sz, key_sz);
    lv_obj_set_style_shadow_width(answer_btn, 0, 0);
    lv_obj_set_style_radius(answer_btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(answer_btn, lv_color_hex(0x2E7D32), 0);
    lv_obj_set_pos(answer_btn, BT_UI_WIDTH - 158, CONTENT_H - key_sz - 54);
    lv_obj_set_user_data(answer_btn, &s_dialer_call_ctx);
    lv_obj_add_event_cb(answer_btn, dialer_answer_click_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *answer_lbl = lv_label_create(answer_btn);
    lv_label_set_text(answer_lbl, LV_SYMBOL_CALL);
    lv_obj_center(answer_lbl);
    lv_obj_set_style_text_color(answer_lbl, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(answer_lbl, BT_UI_FONT_ICON, 0);
    lv_obj_add_flag(answer_btn, LV_OBJ_FLAG_HIDDEN);

    static bt_ui_dialer_refs_t s_dialer_refs;
    s_dialer_refs.number_label = dialer_number_label;
    s_dialer_refs.call_btn = call_btn;
    s_dialer_refs.call_btn_label = call_lbl;
    s_dialer_refs.answer_btn = answer_btn;
    s_dialer_refs.back_btn = back_btn;
    s_dialer_refs.call_ctx = &s_dialer_call_ctx;
    lv_obj_set_user_data(root, &s_dialer_refs);

    return root;
}

void bt_ui_dialer_set_call_state(lv_obj_t *dialer_root, int state, const char *number)
{
    if (dialer_root == NULL) {
        return;
    }
    bt_ui_dialer_refs_t *refs = (bt_ui_dialer_refs_t *)lv_obj_get_user_data(dialer_root);
    if (refs == NULL || refs->number_label == NULL) {
        return;
    }
    const char *state_text = "";
    switch (state) {
        case 1:
            state_text = "Incoming";
            break;
        case 2:
            state_text = "Dialing...";
            break;
        case 3:
            state_text = "Ringing...";
            break;
        case 4:
            state_text = "In call";
            break;
        case 5:
        case 6:
        case 7:
            state_text = "Held";
            break;
        default:
            break;
    }
    if (state != 0 && number != NULL && number[0] != '\0') {
        lv_label_set_text_fmt(refs->number_label, "%s  %s", state_text, number);
    } else if (state != 0) {
        lv_label_set_text(refs->number_label, state_text);
    } else {
        dialer_number_buf[0] = '\0';
        lv_label_set_text(refs->number_label, "Enter Number");
    }
    dialer_call_ctx_t *ctx = (dialer_call_ctx_t *)refs->call_ctx;
    if (ctx != NULL) {
        ctx->in_call = state != 0;
    }

    bool incoming = (state == ESP_BT_AUDIO_CALL_STATE_INCOMING);
    bool active = (state != 0);

    if (refs->call_btn != NULL) {
        lv_obj_set_style_bg_color(refs->call_btn, lv_color_hex(active ? 0xc62828 : 0x2E7D32), 0);
    }
    if (refs->call_btn_label != NULL) {
        lv_label_set_text(refs->call_btn_label, active ? LV_SYMBOL_CLOSE : LV_SYMBOL_CALL);
    }

    if (refs->answer_btn != NULL) {
        if (incoming) {
            lv_obj_clear_flag(refs->answer_btn, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(refs->answer_btn, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (refs->back_btn != NULL) {
        if (active) {
            lv_obj_add_flag(refs->back_btn, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_flag(refs->back_btn, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

/* ====== PHOTO GALLERY PAGE (Animated GIF frames) ====== */

typedef struct {
    lv_obj_t   *root;
    lv_obj_t   *img_obj;
    lv_obj_t   *counter_label;
    int         current_idx;
    int         current_frame;
    lv_timer_t *anim_timer;
} bt_ui_gallery_refs_t;

static bt_ui_gallery_refs_t s_gallery_refs;

static void gallery_anim_timer_cb(lv_timer_t *t)
{
    bt_ui_gallery_refs_t *refs = (bt_ui_gallery_refs_t *)lv_timer_get_user_data(t);
    if (refs == NULL || refs->img_obj == NULL) return;
    int idx = refs->current_idx;
    if (idx < 0 || idx >= GALLERY_IMAGE_COUNT) return;
    const gallery_anim_info_t *anim = &gallery_anims[idx];
    if (anim->frame_count <= 1) return;
    refs->current_frame = (refs->current_frame + 1) % anim->frame_count;
    lv_image_set_src(refs->img_obj, anim->frames[refs->current_frame]);
    lv_timer_set_period(t, anim->frame_delay_ms);
}

static void gallery_stop_anim(void)
{
    if (s_gallery_refs.anim_timer != NULL) {
        lv_timer_del(s_gallery_refs.anim_timer);
        s_gallery_refs.anim_timer = NULL;
    }
}

static void gallery_start_anim(void)
{
    gallery_stop_anim();
    s_gallery_refs.current_frame = 0;
    int idx = s_gallery_refs.current_idx;
    if (idx < 0 || idx >= GALLERY_IMAGE_COUNT) return;
    const gallery_anim_info_t *anim = &gallery_anims[idx];
    lv_image_set_src(s_gallery_refs.img_obj, anim->frames[0]);
    if (anim->frame_count > 1) {
        s_gallery_refs.anim_timer = lv_timer_create(gallery_anim_timer_cb, anim->frame_delay_ms, &s_gallery_refs);
        lv_timer_set_repeat_count(s_gallery_refs.anim_timer, LV_ANIM_REPEAT_INFINITE);
    }
}

static void gallery_refresh_image(void)
{
    if (s_gallery_refs.img_obj == NULL || s_gallery_refs.current_idx < 0) return;
    int idx = s_gallery_refs.current_idx;
    if (idx >= GALLERY_IMAGE_COUNT) {
        idx = 0;
        s_gallery_refs.current_idx = 0;
    }
    if (s_gallery_refs.counter_label != NULL) {
        lv_label_set_text_fmt(s_gallery_refs.counter_label, "%d / %d",
                              s_gallery_refs.current_idx + 1, GALLERY_IMAGE_COUNT);
    }
    /* Match gallery background to GIF dominant color (sample first pixel) */
    if (s_gallery_refs.root != NULL && idx < GALLERY_IMAGE_COUNT) {
        const lv_image_dsc_t *img = gallery_anims[idx].frames[0];
        if (img && img->data && img->data_size >= 2) {
            const uint8_t *d = (const uint8_t *)img->data;
            uint16_t px = ((uint16_t)d[1] << 8) | d[0];
            uint8_t r = ((px >> 11) & 0x1F) * 255 / 31;
            uint8_t g = ((px >> 5) & 0x3F) * 255 / 63;
            uint8_t b = (px & 0x1F) * 255 / 31;
            /* Darken to 25% for background */
            r = r * 25 / 100;
            g = g * 25 / 100;
            b = b * 25 / 100;
            lv_obj_set_style_bg_color(s_gallery_refs.root, lv_color_make(r, g, b), 0);
        }
    }
    gallery_start_anim();
}

static void gallery_prev_click_cb(lv_event_t *e)
{
    (void)e;
    if (s_gallery_refs.current_idx > 0) {
        s_gallery_refs.current_idx--;
    } else {
        s_gallery_refs.current_idx = GALLERY_IMAGE_COUNT - 1;
    }
    gallery_refresh_image();
}

static void gallery_next_click_cb(lv_event_t *e)
{
    (void)e;
    if (s_gallery_refs.current_idx < GALLERY_IMAGE_COUNT - 1) {
        s_gallery_refs.current_idx++;
    } else {
        s_gallery_refs.current_idx = 0;
    }
    gallery_refresh_image();
}

lv_obj_t *bt_ui_gallery_create(lv_obj_t *parent)
{
    memset(&s_gallery_refs, 0, sizeof(s_gallery_refs));

    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, BT_UI_WIDTH, BT_UI_HEIGHT);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(root, lv_color_hex(0x102840), 0);

    s_gallery_refs.root = root;
    s_gallery_refs.current_idx = 0;

    /* Image container (fills most of screen) */
    lv_obj_t *img_cont = lv_obj_create(root);
    lv_obj_remove_style_all(img_cont);
    lv_obj_set_size(img_cont, 700, 440);
    lv_obj_center(img_cont);
    lv_obj_clear_flag(img_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(img_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(img_cont, 0, 0);
    lv_obj_set_style_pad_all(img_cont, 0, 0);

    /* Image widget inside container — scales GIF to 5x (native ~64x64 → ~320x320) */
#if LVGL_VERSION_MAJOR >= 9
    lv_obj_t *img = lv_image_create(img_cont);
    lv_image_set_scale_x(img, 1280);  /* 256 * 5 = 1280 → 5x zoom */
    lv_image_set_scale_y(img, 1280);
#else
    lv_obj_t *img = lv_img_create(img_cont);
    lv_img_set_zoom(img, 1280);
#endif
    lv_obj_set_size(img, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_center(img);
    s_gallery_refs.img_obj = img;

    /* Left arrow button */
#if LVGL_VERSION_MAJOR >= 9
    lv_obj_t *btn_prev = lv_button_create(root);
#else
    lv_obj_t *btn_prev = lv_btn_create(root);
#endif
    lv_obj_set_size(btn_prev, 64, 96);
    lv_obj_align(btn_prev, LV_ALIGN_LEFT_MID, 28, 0);
    lv_obj_set_style_radius(btn_prev, 16, 0);
    lv_obj_set_style_bg_color(btn_prev, lv_color_hex(0x334455), 0);
    lv_obj_set_style_bg_opa(btn_prev, LV_OPA_70, 0);
    lv_obj_set_style_shadow_width(btn_prev, 0, 0);
    lv_obj_set_style_border_width(btn_prev, 0, 0);
    lv_obj_t *lbl_prev = lv_label_create(btn_prev);
    lv_label_set_text(lbl_prev, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_font(lbl_prev, BT_UI_FONT_COVER_ICON, 0);
    lv_obj_set_style_text_color(lbl_prev, lv_color_hex(0xffffff), 0);
    lv_obj_center(lbl_prev);
    lv_obj_add_event_cb(btn_prev, gallery_prev_click_cb, LV_EVENT_CLICKED, NULL);

    /* Right arrow button */
#if LVGL_VERSION_MAJOR >= 9
    lv_obj_t *btn_next = lv_button_create(root);
#else
    lv_obj_t *btn_next = lv_btn_create(root);
#endif
    lv_obj_set_size(btn_next, 64, 96);
    lv_obj_align(btn_next, LV_ALIGN_RIGHT_MID, -28, 0);
    lv_obj_set_style_radius(btn_next, 16, 0);
    lv_obj_set_style_bg_color(btn_next, lv_color_hex(0x334455), 0);
    lv_obj_set_style_bg_opa(btn_next, LV_OPA_70, 0);
    lv_obj_set_style_shadow_width(btn_next, 0, 0);
    lv_obj_set_style_border_width(btn_next, 0, 0);
    lv_obj_t *lbl_next = lv_label_create(btn_next);
    lv_label_set_text(lbl_next, LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_font(lbl_next, BT_UI_FONT_COVER_ICON, 0);
    lv_obj_set_style_text_color(lbl_next, lv_color_hex(0xffffff), 0);
    lv_obj_center(lbl_next);
    lv_obj_add_event_cb(btn_next, gallery_next_click_cb, LV_EVENT_CLICKED, NULL);

    /* Counter label (bottom center) */
    lv_obj_t *counter = lv_label_create(root);
    lv_label_set_text_fmt(counter, "1 / %d", GALLERY_IMAGE_COUNT);
    lv_obj_set_style_text_color(counter, lv_color_hex(0x8899aa), 0);
    lv_obj_set_style_text_font(counter, BT_UI_FONT_SMALL, 0);
    lv_obj_align(counter, LV_ALIGN_BOTTOM_MID, 0, -20);
    s_gallery_refs.counter_label = counter;

    lv_obj_set_user_data(root, &s_gallery_refs);

    /* Start animation on the first image */
    gallery_start_anim();

    return root;
}

void bt_ui_gallery_next(lv_obj_t *gallery_root)
{
    (void)gallery_root;
    if (s_gallery_refs.current_idx < GALLERY_IMAGE_COUNT - 1) {
        s_gallery_refs.current_idx++;
    } else {
        s_gallery_refs.current_idx = 0;
    }
    gallery_refresh_image();
}

void bt_ui_gallery_prev(lv_obj_t *gallery_root)
{
    (void)gallery_root;
    if (s_gallery_refs.current_idx > 0) {
        s_gallery_refs.current_idx--;
    } else {
        s_gallery_refs.current_idx = GALLERY_IMAGE_COUNT - 1;
    }
    gallery_refresh_image();
}

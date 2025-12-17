/**
 * @file ui_init.c
 * @brief LVGL, display, and touch initialization implementation
 */

#include "ui_init.h"
#include "config/app_config.h"
#include "i2c/i2c_manager.h"
#include "ui/ui_events.h"
#include "esp_log.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_sh8601.h"
#include "esp_timer.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "SensorLib.h"
#include "TouchDrvCST92xx.h"
#include <string.h>

static const char *TAG = "ui_init";

// UI context
static ui_context_t ui_ctx = {0};

// Display handles
static esp_lcd_panel_handle_t panel_handle = NULL;
static lv_disp_t *disp = NULL;

// Touch controller
static TouchDrvCST92xx touch;
static int16_t touch_x[5], touch_y[5];

// LVGL buffers
static lv_disp_draw_buf_t disp_buf;
static lv_disp_drv_t disp_drv;

// LCD initialization commands for SH8601
static const sh8601_lcd_init_cmd_t lcd_init_cmds[] = {
    {0xFE, (uint8_t[]){0x00}, 1, 0},
    {0xC4, (uint8_t[]){0x80}, 1, 0},
    {0x3A, (uint8_t[]){0x55}, 1, 0},
    {0x35, (uint8_t[]){0x00}, 1, 0},
    {0x53, (uint8_t[]){0x20}, 1, 0},
    {0x51, (uint8_t[]){0xFF}, 1, 0},
    {0x63, (uint8_t[]){0xFF}, 1, 0},
    {0x2A, (uint8_t[]){0x00, 0x06, 0x01, 0xD7}, 4, 0},
    {0x2B, (uint8_t[]){0x00, 0x00, 0x01, 0xD1}, 4, 600},
    {0x11, NULL, 0, 600},
    {0x29, NULL, 0, 0},
};

// Forward declarations
static bool notify_lvgl_flush_ready(esp_lcd_panel_io_handle_t panel_io, 
                                    esp_lcd_panel_io_event_data_t *edata, void *user_ctx);
static void lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map);
static void lvgl_rounder_cb(struct _lv_disp_drv_t *disp_drv, lv_area_t *area);
static void lvgl_touch_cb(lv_indev_drv_t *drv, lv_indev_data_t *data);
static void increase_lvgl_tick(void *arg);
static void lvgl_port_task(void *arg);

esp_err_t ui_hardware_init(void)
{
    ESP_LOGI(TAG, "Initializing hardware...");

    // 1. Initialize SPI for display
    ESP_LOGI(TAG, "Initialize SPI bus");
    spi_bus_config_t buscfg = {};
    buscfg.sclk_io_num = LCD_PIN_PCLK;
    buscfg.data0_io_num = LCD_PIN_DATA0;
    buscfg.data1_io_num = LCD_PIN_DATA1;
    buscfg.data2_io_num = LCD_PIN_DATA2;
    buscfg.data3_io_num = LCD_PIN_DATA3;
    buscfg.max_transfer_sz = LCD_H_RES * LCD_V_RES * sizeof(uint16_t);
    buscfg.flags = SPICOMMON_BUSFLAG_QUAD;
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));

    // 2. Install panel IO
    ESP_LOGI(TAG, "Install panel IO");
    esp_lcd_panel_io_handle_t io_handle = NULL;
    const esp_lcd_panel_io_spi_config_t io_config = 
        SH8601_PANEL_IO_QSPI_CONFIG(LCD_PIN_CS, notify_lvgl_flush_ready, &disp_drv);
    
    sh8601_vendor_config_t vendor_config = {
        .init_cmds = lcd_init_cmds,
        .init_cmds_size = sizeof(lcd_init_cmds) / sizeof(lcd_init_cmds[0]),
        .flags = {
            .use_qspi_interface = 1,
        },
    };

    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, 
                                              &io_config, &io_handle));

    // 3. Install LCD driver
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = LCD_PIN_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = LCD_BIT_PER_PIXEL,
        .vendor_config = &vendor_config,
    };
    
    ESP_LOGI(TAG, "Install SH8601 panel driver");
    ESP_ERROR_CHECK(esp_lcd_new_panel_sh8601(io_handle, &panel_config, &panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

    // 4. Verify I2C bus is initialized (shared resource managed by i2c_manager)
    // I2C should be initialized by main app before hardware init
    if (!i2c_shared_is_initialized()) {
        ESP_LOGE(TAG, "I2C bus not initialized! Call i2c_shared_init() first");
        return ESP_ERR_INVALID_STATE;
    }
    ESP_LOGI(TAG, "Using shared I2C bus (I2C_NUM_%d) for touch controller", i2c_shared_get_port());

    // 5. Setup touch controller
    touch.setPins(TOUCH_RST_IO, TOUCH_INT_IO);
    if (!touch.begin(I2C_NUM, TOUCH_ADDR, I2C_SDA_IO, I2C_SCL_IO)) {
        ESP_LOGE(TAG, "Failed to initialize touch controller");
        return ESP_FAIL;
    }
    touch.reset();
    touch.setMaxCoordinates(LCD_H_RES, LCD_V_RES);
    touch.setMirrorXY(true, true);
    ESP_LOGI(TAG, "Touch controller initialized");

    ESP_LOGI(TAG, "Hardware initialization complete");
    return ESP_OK;
}

esp_err_t ui_lvgl_init(void)
{
    ESP_LOGI(TAG, "Initializing LVGL...");

    // 1. Initialize LVGL library
    lv_init();

    // 2. Allocate draw buffers in internal DMA-capable RAM.
    // Rationale: the SPI driver requires DMA-capable tx buffers. If LVGL's color_map is in PSRAM,
    // the SPI driver will allocate a temporary DMA "bounce buffer" per flush; that can fail at
    // runtime and prevent LVGL from completing press/release/click handling.
    const size_t buf_bytes = (size_t)LCD_H_RES * (size_t)LVGL_BUF_HEIGHT * sizeof(lv_color_t);
    ESP_LOGI(TAG, "Allocating LVGL draw buffers: %dx%d lines, %u bytes per buffer",
             LCD_H_RES, LVGL_BUF_HEIGHT, (unsigned)buf_bytes);

    lv_color_t *buf1 = (lv_color_t *)heap_caps_malloc(buf_bytes, MALLOC_CAP_DMA);
    if (!buf1) {
        ESP_LOGE(TAG, "Failed to allocate LVGL buffer 1 in DMA RAM");
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "LVGL buffer 1 allocated: %u bytes in DMA RAM", (unsigned)buf_bytes);

    // Try double buffering; if the second DMA allocation fails, fall back to single buffering.
    lv_color_t *buf2 = (lv_color_t *)heap_caps_malloc(buf_bytes, MALLOC_CAP_DMA);
    if (!buf2) {
        ESP_LOGW(TAG, "Failed to allocate LVGL buffer 2 in DMA RAM; falling back to single buffer");
        lv_disp_draw_buf_init(&disp_buf, buf1, NULL, LCD_H_RES * LVGL_BUF_HEIGHT);
    } else {
        ESP_LOGI(TAG, "LVGL buffer 2 allocated: %u bytes in DMA RAM", (unsigned)buf_bytes);
        lv_disp_draw_buf_init(&disp_buf, buf1, buf2, LCD_H_RES * LVGL_BUF_HEIGHT);
    }

    // 3. Register display driver
    ESP_LOGI(TAG, "Register display driver");
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = LCD_H_RES;
    disp_drv.ver_res = LCD_V_RES;
    disp_drv.flush_cb = lvgl_flush_cb;
    disp_drv.rounder_cb = lvgl_rounder_cb;
    disp_drv.draw_buf = &disp_buf;
    disp_drv.user_data = panel_handle;
    disp = lv_disp_drv_register(&disp_drv);
    
    ui_ctx.display = disp;

    // 4. Register touch input device
    ESP_LOGI(TAG, "Register touch input device");
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.disp = disp;
    indev_drv.read_cb = lvgl_touch_cb;
    ui_ctx.touch_indev = lv_indev_drv_register(&indev_drv);

    // 5. Install LVGL tick timer
    ESP_LOGI(TAG, "Install LVGL tick timer");
    const esp_timer_create_args_t lvgl_tick_timer_args = {
        .callback = &increase_lvgl_tick,
        .name = "lvgl_tick"
    };
    esp_timer_handle_t lvgl_tick_timer = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(lvgl_tick_timer, LVGL_TICK_PERIOD_MS * 1000));

    // 6. Create LVGL mutex
    ui_ctx.lvgl_mutex = xSemaphoreCreateMutex();
    if (!ui_ctx.lvgl_mutex) {
        ESP_LOGE(TAG, "Failed to create LVGL mutex");
        return ESP_FAIL;
    }

    // 7. Create LVGL task
    xTaskCreate(lvgl_port_task, "LVGL", LVGL_TASK_STACK_SIZE, NULL, 
                LVGL_TASK_PRIORITY, NULL);

    ESP_LOGI(TAG, "LVGL initialized");
    return ESP_OK;
}

bool ui_lock(int timeout_ms)
{
    if (!ui_ctx.lvgl_mutex) {
        ESP_LOGE(TAG, "LVGL mutex not initialized");
        return false;
    }

    const TickType_t timeout_ticks = (timeout_ms == -1) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTake(ui_ctx.lvgl_mutex, timeout_ticks) == pdTRUE;
}

void ui_unlock(void)
{
    if (ui_ctx.lvgl_mutex) {
        xSemaphoreGive(ui_ctx.lvgl_mutex);
    }
}

ui_context_t* ui_get_context(void)
{
    return &ui_ctx;
}

void ui_deinit(void)
{
    if (ui_ctx.lvgl_mutex) {
        vSemaphoreDelete(ui_ctx.lvgl_mutex);
        ui_ctx.lvgl_mutex = NULL;
    }
    ESP_LOGI(TAG, "UI deinitialized");
}

// Callback: notify LVGL flush ready (called from ISR - NO LOGGING!)
static bool notify_lvgl_flush_ready(esp_lcd_panel_io_handle_t panel_io, 
                                    esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
    lv_disp_drv_t *disp_driver = (lv_disp_drv_t *)user_ctx;
    lv_disp_flush_ready(disp_driver);
    return false;
}

// Callback: LVGL flush
static void lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    esp_lcd_panel_handle_t panel = (esp_lcd_panel_handle_t)drv->user_data;
    const int offsetx1 = area->x1;
    const int offsetx2 = area->x2;
    const int offsety1 = area->y1;
    const int offsety2 = area->y2;
    
    int width = offsetx2 - offsetx1 + 1;
    int height = offsety2 - offsety1 + 1;
    int pixels = width * height;
    
    // Throttle flush debug logs to avoid spamming the UART (flush can be very frequent).
    // Errors will always be logged below.
    static uint32_t s_flush_cnt = 0;
    s_flush_cnt++;
    if ((s_flush_cnt % 50) == 0) {
        ESP_LOGD(TAG, "Flush[%lu]: area(%d,%d)->(%d,%d) %dx%d px=%d buf=%p",
                 (unsigned long)s_flush_cnt,
                 offsetx1, offsety1, offsetx2, offsety2, width, height, pixels, color_map);
    }

#if LCD_BIT_PER_PIXEL == 24
    // Convert RGB565 to RGB888 for 24-bit mode
    uint8_t *to = (uint8_t *)color_map;
    uint16_t pixel_num = pixels;
    
    // First pixel
    uint8_t temp = color_map[0].ch.blue;
    *to++ = color_map[0].ch.red;
    *to++ = color_map[0].ch.green;
    *to++ = temp;
    
    // Rest of pixels
    for (int i = 1; i < pixel_num; i++) {
        *to++ = color_map[i].ch.red;
        *to++ = color_map[i].ch.green;
        *to++ = color_map[i].ch.blue;
    }
#endif

    esp_err_t ret = esp_lcd_panel_draw_bitmap(panel, offsetx1, offsety1, offsetx2 + 1, offsety2 + 1, color_map);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG,
                 "esp_lcd_panel_draw_bitmap FAILED: %s (0x%x) area(%d,%d)->(%d,%d) %dx%d px=%d buf=%p",
                 esp_err_to_name(ret), ret,
                 offsetx1, offsety1, offsetx2, offsety2, width, height, pixels, color_map);
    }
}

// Callback: LVGL rounder (align to 2-pixel boundaries)
static void lvgl_rounder_cb(struct _lv_disp_drv_t *disp_drv, lv_area_t *area)
{
    uint16_t x1 = area->x1;
    uint16_t x2 = area->x2;
    uint16_t y1 = area->y1;
    uint16_t y2 = area->y2;

    area->x1 = (x1 >> 1) << 1;
    area->y1 = (y1 >> 1) << 1;
    area->x2 = ((x2 >> 1) << 1) + 1;
    area->y2 = ((y2 >> 1) << 1) + 1;
}

// Callback: LVGL touch input
static void lvgl_touch_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    static bool last_pressed = false;
    uint8_t touched = touch.getPoint(touch_x, touch_y, 2);
    if (touched) {
        data->point.x = touch_x[0];
        data->point.y = touch_y[0];
        data->state = LV_INDEV_STATE_PRESSED;
        
        if (!last_pressed) {
            ESP_LOGD(TAG, "Touch PRESSED at (%d, %d)", touch_x[0], touch_y[0]);
            last_pressed = true;
        }
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
        
        if (last_pressed) {
            ESP_LOGD(TAG, "Touch RELEASED");
            last_pressed = false;
        }
    }
}

// Callback: LVGL tick increase
static void increase_lvgl_tick(void *arg)
{
    lv_tick_inc(LVGL_TICK_PERIOD_MS);
}

// Task: LVGL handler
static void lvgl_port_task(void *arg)
{
    ESP_LOGI(TAG, "LVGL task started");
    uint32_t task_delay_ms = LVGL_TASK_MAX_DELAY_MS;
    
    while (1) {
        if (ui_lock(-1)) {
            task_delay_ms = lv_timer_handler();
            // Apply any pending UI updates from other tasks (SDK callbacks, audio, etc.).
            // Important: do this on the LVGL task to avoid ui_lock re-entrancy.
            ui_events_process_lvgl();
            ui_unlock();
        }
        
        if (task_delay_ms > LVGL_TASK_MAX_DELAY_MS) {
            task_delay_ms = LVGL_TASK_MAX_DELAY_MS;
        } else if (task_delay_ms < LVGL_TASK_MIN_DELAY_MS) {
            task_delay_ms = LVGL_TASK_MIN_DELAY_MS;
        }
        
        vTaskDelay(pdMS_TO_TICKS(task_delay_ms));
    }
}


/*
 * SPDX-FileCopyrightText: 2022-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include <inttypes.h>

#include "esp_system.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_interface.h"

#include "lv_port.h"
#include "lvgl.h"

#ifdef ESP_LVGL_PORT_TOUCH_COMPONENT
#include "esp_lcd_touch.h"
#endif

#if (ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(4, 4, 4)) || (ESP_IDF_VERSION == ESP_IDF_VERSION_VAL(5, 0, 0))
#define LVGL_PORT_HANDLE_FLUSH_READY 0
#else
#define LVGL_PORT_HANDLE_FLUSH_READY 1
#endif

static const char *TAG = "LVGL";

/*******************************************************************************
* Types definitions
*******************************************************************************/

typedef struct lvgl_port_ctx_s {
    SemaphoreHandle_t   lvgl_mux;
    esp_timer_handle_t  tick_timer;
    bool                running;
    int                 task_max_sleep_ms;
} lvgl_port_ctx_t;

typedef struct {
    esp_lcd_panel_io_handle_t io_handle;    /* LCD panel IO handle */
    esp_lcd_panel_handle_t    panel_handle; /* LCD panel handle */
    lv_display_t              *disp;        /* LVGL display object (v9) */

    uint32_t                  trans_size;       /* Maximum size for one transport (pixels) */
    uint16_t                  *trans_buf_1;     /* Buffer send to driver */
    uint16_t                  *trans_buf_2;     /* Buffer send to driver */
    uint16_t                  *trans_act;       /* Active buffer for sending to driver */
    SemaphoreHandle_t         trans_done_sem;   /* Semaphore for signaling idle transfer */
    lv_display_rotation_t     sw_rotate;        /* Panel software rotation mask */
    int32_t                   hres;             /* Resolution AFTER rotation */
    int32_t                   vres;             /* Resolution AFTER rotation */

    lvgl_port_wait_cb         draw_wait_cb;     /* Callback function for drawing */
} lvgl_port_display_ctx_t;

#ifdef ESP_LVGL_PORT_TOUCH_COMPONENT
typedef struct {
    esp_lcd_touch_handle_t  handle;        /* LCD touch IO handle */
    lv_indev_t              *indev;        /* LVGL input device object (v9) */
    lvgl_port_wait_cb       touch_wait_cb;  /* Callback function for touch */
} lvgl_port_touch_ctx_t;
#endif

/*******************************************************************************
* Local variables
*******************************************************************************/
static lvgl_port_ctx_t lvgl_port_ctx;
static int lvgl_port_timer_period_ms = 5;

#if defined(NIGHT_TTF_BENCHMARK)
static int64_t s_benchmark_start_us;
static uint64_t s_benchmark_handler_us;
static uint32_t s_benchmark_handler_count;
static uint32_t s_benchmark_handler_max_us;
#endif

/*******************************************************************************
* Function definitions
*******************************************************************************/
static void lvgl_port_task(void *arg);
static esp_err_t lvgl_port_tick_init(void);
static void lvgl_port_task_deinit(void);

// LVGL callbacks
#if LVGL_PORT_HANDLE_FLUSH_READY
static bool lvgl_port_flush_ready_callback(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx);
#endif
static void lvgl_port_flush_callback(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map);
#ifdef ESP_LVGL_PORT_TOUCH_COMPONENT
static void lvgl_port_touchpad_read(lv_indev_t *indev, lv_indev_data_t *data);
#endif
/*******************************************************************************
* Public API functions
*******************************************************************************/

esp_err_t lvgl_port_init(const lvgl_port_cfg_t *cfg)
{
    esp_err_t ret = ESP_OK;
    ESP_GOTO_ON_FALSE(cfg, ESP_ERR_INVALID_ARG, err, TAG, "invalid argument");
    ESP_GOTO_ON_FALSE(cfg->task_affinity < (configNUM_CORES), ESP_ERR_INVALID_ARG, err, TAG, "Bad core number for task! Maximum core number is %d", (configNUM_CORES - 1));

    memset(&lvgl_port_ctx, 0, sizeof(lvgl_port_ctx));

    /* LVGL init */
    lv_init();
    /* Tick init */
    lvgl_port_timer_period_ms = cfg->timer_period_ms;
    ESP_RETURN_ON_ERROR(lvgl_port_tick_init(), TAG, "");
    /* Create task */
    lvgl_port_ctx.task_max_sleep_ms = cfg->task_max_sleep_ms;
    if (lvgl_port_ctx.task_max_sleep_ms == 0) {
        lvgl_port_ctx.task_max_sleep_ms = 500;
    }
    lvgl_port_ctx.lvgl_mux = xSemaphoreCreateRecursiveMutex();
    ESP_GOTO_ON_FALSE(lvgl_port_ctx.lvgl_mux, ESP_ERR_NO_MEM, err, TAG, "Create LVGL mutex fail!");

    BaseType_t res;
    if (cfg->task_affinity < 0) {
        res = xTaskCreate(lvgl_port_task, "LVGL task", cfg->task_stack, NULL, cfg->task_priority, NULL);
    } else {
        res = xTaskCreatePinnedToCore(lvgl_port_task, "LVGL task", cfg->task_stack, NULL, cfg->task_priority, NULL, cfg->task_affinity);
    }
    ESP_GOTO_ON_FALSE(res == pdPASS, ESP_FAIL, err, TAG, "Create LVGL task fail!");

err:
    if (ret != ESP_OK) {
        lvgl_port_deinit();
    }

    return ret;
}

void lvgl_port_set_sw_rotation(lv_display_t *disp, lv_display_rotation_t rotation)
{
    if (disp == NULL) return;
    lvgl_port_display_ctx_t *ctx = (lvgl_port_display_ctx_t *)lv_display_get_user_data(disp);
    if (ctx == NULL) return;

    /* Solo se admite alternar entre 90 y 270, que son las dos orientaciones
     * apaisadas: al no cambiar hres/vres basta con cambiar como rota el flush,
     * sin rehacer el display ni los buffers. */
    if (rotation != LV_DISPLAY_ROTATION_90 && rotation != LV_DISPLAY_ROTATION_270) return;
    ctx->sw_rotate = rotation;
}

esp_err_t lvgl_port_resume(void)
{
    esp_err_t ret = ESP_ERR_INVALID_STATE;

    if (lvgl_port_ctx.tick_timer != NULL) {
        lv_timer_enable(true);
        ret = esp_timer_start_periodic(lvgl_port_ctx.tick_timer, lvgl_port_timer_period_ms * 1000);
    }

    return ret;
}

esp_err_t lvgl_port_stop(void)
{
    esp_err_t ret = ESP_ERR_INVALID_STATE;

    if (lvgl_port_ctx.tick_timer != NULL) {
        lv_timer_enable(false);
        ret = esp_timer_stop(lvgl_port_ctx.tick_timer);
    }

    return ret;
}

esp_err_t lvgl_port_deinit(void)
{
    /* Stop and delete timer */
    if (lvgl_port_ctx.tick_timer != NULL) {
        esp_timer_stop(lvgl_port_ctx.tick_timer);
        esp_timer_delete(lvgl_port_ctx.tick_timer);
        lvgl_port_ctx.tick_timer = NULL;
    }

    /* Stop running task */
    if (lvgl_port_ctx.running) {
        lvgl_port_ctx.running = false;
    } else {
        lvgl_port_task_deinit();
    }

    return ESP_OK;
}

lv_display_t *lvgl_port_add_disp(const lvgl_port_display_cfg_t *disp_cfg)
{
    esp_err_t ret = ESP_OK;
    lv_display_t *disp = NULL;
    uint16_t *buf1 = NULL;
    uint16_t *buf2 = NULL;
    uint16_t *buf3 = NULL;
    SemaphoreHandle_t trans_done_sem = NULL;

    assert(disp_cfg != NULL);
    assert(disp_cfg->io_handle != NULL);
    assert(disp_cfg->panel_handle != NULL);
    assert(disp_cfg->buffer_size > 0);
    assert(disp_cfg->hres > 0);
    assert(disp_cfg->vres > 0);

    /* Display context */
    lvgl_port_display_ctx_t *disp_ctx = malloc(sizeof(lvgl_port_display_ctx_t));
    ESP_GOTO_ON_FALSE(disp_ctx, ESP_ERR_NO_MEM, err, TAG, "Not enough memory for display context allocation!");
    disp_ctx->io_handle = disp_cfg->io_handle;
    disp_ctx->panel_handle = disp_cfg->panel_handle;
    disp_ctx->trans_size = disp_cfg->trans_size;
    disp_ctx->sw_rotate = disp_cfg->sw_rotate;
    disp_ctx->draw_wait_cb = disp_cfg->draw_wait_cb;
    /* In v9 the flush callback no longer receives a driver struct carrying
     * hor_res/ver_res, so the rotated resolution is cached here instead. */
    disp_ctx->hres = disp_cfg->hres;
    disp_ctx->vres = disp_cfg->vres;

    uint32_t buff_caps = MALLOC_CAP_DEFAULT;
    if (disp_cfg->flags.buff_dma) {
        buff_caps = MALLOC_CAP_DMA;
    } else if (disp_cfg->flags.buff_spiram) {
        buff_caps = MALLOC_CAP_SPIRAM;
    }

    /* alloc draw buffers used by LVGL */
    /* it's recommended to choose the size of the draw buffer(s) to be at least 1/10 screen sized */
    buf1 = heap_caps_malloc(disp_cfg->buffer_size * sizeof(uint16_t), buff_caps);
    ESP_GOTO_ON_FALSE(buf1, ESP_ERR_NO_MEM, err, TAG, "Not enough memory for LVGL buffer (buf1) allocation!");

    if (disp_ctx->trans_size) {

        uint32_t caps = MALLOC_CAP_DMA;

        buf2 = heap_caps_malloc(disp_ctx->trans_size * sizeof(uint16_t), caps);
        ESP_GOTO_ON_FALSE(buf2, ESP_ERR_NO_MEM, err, TAG, "Not enough memory for buffer(transport) allocation!");
        disp_ctx->trans_buf_1 = buf2;

        buf3 = heap_caps_malloc(disp_ctx->trans_size * sizeof(uint16_t), caps);
        ESP_GOTO_ON_FALSE(buf3, ESP_ERR_NO_MEM, err, TAG, "Not enough memory for buffer(transport) allocation!");
        disp_ctx->trans_buf_2 = buf3;

        /* Arranca en 1 = "el DMA esta libre": el flush lo coge antes de cada
         * envio y la ISR de fin de transferencia lo devuelve. */
        trans_done_sem = xSemaphoreCreateCounting(1, 1);
        ESP_GOTO_ON_FALSE(trans_done_sem, ESP_ERR_NO_MEM, err, TAG, "Failed to create transport counting Semaphore");
        disp_ctx->trans_done_sem = trans_done_sem;
    }

    ESP_LOGD(TAG, "Register display driver to LVGL");
    disp = lv_display_create(disp_cfg->hres, disp_cfg->vres);
    ESP_GOTO_ON_FALSE(disp, ESP_ERR_NO_MEM, err, TAG, "lv_display_create failed!");
    disp_ctx->disp = disp;

    lv_display_set_flush_cb(disp, lvgl_port_flush_callback);
    lv_display_set_user_data(disp, disp_ctx);

    /* v8 set this globally with LV_COLOR_16_SWAP 1; in v9 the byte order is a
     * per-display colour format. The ST77922 over QSPI expects big-endian
     * RGB565, so keep the swapped variant. */
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565_SWAPPED);

    /* FULL: LVGL entrega siempre el fotograma completo.
     *
     * Se intento PARTIAL para ganar velocidad y hubo que descartarlo: el flush
     * de este port solo produce imagen correcta cuando el area es la pantalla
     * entera. Con cualquier area menor -incluso ocupando el ancho completo- el
     * contenido sale desplazado y la pantalla se llena de ruido diagonal. El
     * fallo NO esta en el envio al panel (se comprobo rellenando el bloque ya
     * rotado con un color fijo: sale limpio y en su sitio), sino en como el
     * flush interpreta el buffer que le entrega LVGL. */
    lv_display_set_buffers(disp, buf1, NULL,
                           disp_cfg->buffer_size * sizeof(uint16_t),
                           LV_DISPLAY_RENDER_MODE_FULL);

#if LVGL_PORT_HANDLE_FLUSH_READY
    /* Register done callback */
    const esp_lcd_panel_io_callbacks_t cbs = {
        .on_color_trans_done = lvgl_port_flush_ready_callback,
    };
    esp_lcd_panel_io_register_event_callbacks(disp_ctx->io_handle, &cbs, disp_ctx);
#endif

err:
    if (ret != ESP_OK) {
        if (buf1) {
            free(buf1);
        }
        if (buf2) {
            free(buf2);
        }
        if (buf3) {
            free(buf3);
        }
        if (trans_done_sem) {
            vSemaphoreDelete(trans_done_sem);
        }
        if (disp_ctx) {
            free(disp_ctx);
        }
    }

    return disp;
}

esp_err_t lvgl_port_remove_disp(lv_display_t *disp)
{
    assert(disp);
    lvgl_port_display_ctx_t *disp_ctx = (lvgl_port_display_ctx_t *)lv_display_get_user_data(disp);

    /* In v9 the draw buffer is owned by the display; grab the pointer before
     * deleting it, then free the memory we allocated ourselves. */
    lv_draw_buf_t *draw_buf = lv_display_get_buf_active(disp);
    void *buf_mem = (draw_buf ? draw_buf->unaligned_data : NULL);

    lv_display_delete(disp);

    if (buf_mem) {
        free(buf_mem);
    }

    if (disp_ctx) {
        if (disp_ctx->trans_buf_1) {
            free(disp_ctx->trans_buf_1);
        }
        if (disp_ctx->trans_buf_2) {
            free(disp_ctx->trans_buf_2);
        }
        if (disp_ctx->trans_done_sem) {
            vSemaphoreDelete(disp_ctx->trans_done_sem);
        }
        free(disp_ctx);
    }

    return ESP_OK;
}

#ifdef ESP_LVGL_PORT_TOUCH_COMPONENT
lv_indev_t *lvgl_port_add_touch(const lvgl_port_touch_cfg_t *touch_cfg)
{
    assert(touch_cfg != NULL);
    assert(touch_cfg->disp != NULL);
    assert(touch_cfg->handle != NULL);

    /* Touch context */
    lvgl_port_touch_ctx_t *touch_ctx = malloc(sizeof(lvgl_port_touch_ctx_t));
    if (touch_ctx == NULL) {
        ESP_LOGE(TAG, "Not enough memory for touch context allocation!");
        return NULL;
    }
    touch_ctx->handle = touch_cfg->handle;
    touch_ctx->touch_wait_cb = touch_cfg->touch_wait_cb;

    /* Register a touchpad input device */
    touch_ctx->indev = lv_indev_create();
    if (touch_ctx->indev == NULL) {
        ESP_LOGE(TAG, "lv_indev_create failed!");
        free(touch_ctx);
        return NULL;
    }
    lv_indev_set_type(touch_ctx->indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_display(touch_ctx->indev, touch_cfg->disp);
    lv_indev_set_read_cb(touch_ctx->indev, lvgl_port_touchpad_read);
    lv_indev_set_user_data(touch_ctx->indev, touch_ctx);
    return touch_ctx->indev;
}

esp_err_t lvgl_port_remove_touch(lv_indev_t *touch)
{
    assert(touch);
    lvgl_port_touch_ctx_t *touch_ctx = (lvgl_port_touch_ctx_t *)lv_indev_get_user_data(touch);

    /* Remove input device driver */
    lv_indev_delete(touch);

    if (touch_ctx) {
        free(touch_ctx);
    }

    return ESP_OK;
}
#endif

bool lvgl_port_lock(uint32_t timeout_ms)
{
    assert(lvgl_port_ctx.lvgl_mux && "lvgl_port_init must be called first");

    const TickType_t timeout_ticks = (timeout_ms == 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTakeRecursive(lvgl_port_ctx.lvgl_mux, timeout_ticks) == pdTRUE;
}

void lvgl_port_unlock(void)
{
    assert(lvgl_port_ctx.lvgl_mux && "lvgl_port_init must be called first");
    xSemaphoreGiveRecursive(lvgl_port_ctx.lvgl_mux);
}

#if defined(NIGHT_TTF_BENCHMARK)
void lvgl_port_benchmark_reset(void)
{
    s_benchmark_start_us = esp_timer_get_time();
    s_benchmark_handler_us = 0;
    s_benchmark_handler_count = 0;
    s_benchmark_handler_max_us = 0;
}

void lvgl_port_benchmark_print(void)
{
    const int64_t elapsed_us = esp_timer_get_time() - s_benchmark_start_us;
    const uint64_t load_x100 = elapsed_us > 0
                             ? (s_benchmark_handler_us * 10000ULL) / (uint64_t)elapsed_us
                             : 0;

    ESP_LOGI(TAG,
             "[bench] window_ms=%" PRId64 " handlers=%" PRIu32
             " handler_us=%" PRIu64 " max_handler_us=%" PRIu32
             " lvgl_load=%" PRIu64 ".%02" PRIu64 "%%"
             " free8=%" PRIu32 " min8=%" PRIu32
             " internal=%" PRIu32 " spiram=%" PRIu32,
             elapsed_us / 1000,
             s_benchmark_handler_count,
             s_benchmark_handler_us,
             s_benchmark_handler_max_us,
             load_x100 / 100,
             load_x100 % 100,
             heap_caps_get_free_size(MALLOC_CAP_8BIT),
             heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT),
             heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
}
#endif

void lvgl_port_flush_ready(lv_display_t *disp)
{
    assert(disp);
    lv_display_flush_ready(disp);
}

/*******************************************************************************
* Private functions
*******************************************************************************/

/* Suelo del descanso de la tarea LVGL: deja respirar al resto del sistema sin
 * estrangular el refresco ni la lectura del tactil. */
#define LVGL_PORT_TASK_MIN_DELAY_MS 2

static void lvgl_port_task(void *arg)
{
    uint32_t task_delay_ms = lvgl_port_ctx.task_max_sleep_ms;

    ESP_LOGI(TAG, "Starting LVGL task");
    lvgl_port_ctx.running = true;
    while (lvgl_port_ctx.running) {
        if (lvgl_port_lock(0)) {
#if defined(NIGHT_TTF_BENCHMARK)
            const int64_t handler_start_us = esp_timer_get_time();
#endif
            task_delay_ms = lv_timer_handler();
#if defined(NIGHT_TTF_BENCHMARK)
            const uint32_t handler_us = (uint32_t)(esp_timer_get_time() - handler_start_us);
            s_benchmark_handler_us += handler_us;
            s_benchmark_handler_count++;
            if (handler_us > s_benchmark_handler_max_us) s_benchmark_handler_max_us = handler_us;
#endif
            lvgl_port_unlock();
        }
        /*
         * CORRECCION AL CODIGO DEL FABRICANTE.
         *
         * El original hacia esto:
         *     if ((task_delay_ms > max_sleep) || (1 == task_delay_ms))
         *         task_delay_ms = max_sleep;      // 500 ms
         *
         * Es decir: cuando lv_timer_handler() contesta "vuelve en 1 ms"
         * -justo cuando LVGL esta ocupada- la tarea se dormia medio segundo.
         * Medido por Serial: el lector del tactil se llamaba 2 o 3 veces por
         * segundo en vez de a 16 ms, con huecos limpios de 510 ms. Un gesto
         * entero se resolvia con dos muestras, asi que ningun detector de
         * deslizamiento podia funcionar, y la animacion del flip caia a 3 fps.
         *
         * Ahora el maximo solo se aplica cuando de verdad no hay trabajo
         * (LV_NO_TIMER_READY), y se pone un minimo para no acaparar la CPU.
         */
        if ((task_delay_ms == LV_NO_TIMER_READY) || (task_delay_ms > lvgl_port_ctx.task_max_sleep_ms)) {
            task_delay_ms = lvgl_port_ctx.task_max_sleep_ms;
        } else if (task_delay_ms < LVGL_PORT_TASK_MIN_DELAY_MS) {
            task_delay_ms = LVGL_PORT_TASK_MIN_DELAY_MS;
        }
        vTaskDelay(pdMS_TO_TICKS(task_delay_ms));
    }

    lvgl_port_task_deinit();

    /* Close task */
    vTaskDelete(NULL);
}

static void lvgl_port_task_deinit(void)
{
    if (lvgl_port_ctx.lvgl_mux) {
        vSemaphoreDelete(lvgl_port_ctx.lvgl_mux);
    }
    memset(&lvgl_port_ctx, 0, sizeof(lvgl_port_ctx));
    /* Deinitialize LVGL (v9: LV_MEM_CUSTOM no longer exists) */
    lv_deinit();
}

#if LVGL_PORT_HANDLE_FLUSH_READY
static bool lvgl_port_flush_ready_callback(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
    BaseType_t taskAwake = pdFALSE;

    lvgl_port_display_ctx_t *disp_ctx = (lvgl_port_display_ctx_t *)user_ctx;
    assert(disp_ctx != NULL);

    if (disp_ctx->trans_done_sem) {
        xSemaphoreGiveFromISR(disp_ctx->trans_done_sem, &taskAwake);
    }

    return false;
}
#endif


static void lvgl_port_flush_callback(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    assert(disp != NULL);
    lvgl_port_display_ctx_t *disp_ctx = (lvgl_port_display_ctx_t *)lv_display_get_user_data(disp);
    assert(disp_ctx != NULL);

    /* v9 hands over a raw byte map; with LV_COLOR_DEPTH 16 it is RGB565,
     * so the rotation code below can keep working on 16-bit pixels. */
    uint16_t *color_map = (uint16_t *)px_map;

    const int x_start = area->x1;
    const int x_end = area->x2;
    const int y_start = area->y1;
    const int y_end = area->y2;
    const int width = x_end - x_start + 1;
    const int height = y_end - y_start + 1;

    uint16_t *from = color_map;
    uint16_t *to = NULL;

    /*
     * El paso de fila del buffer de dibujo NO tiene por que ser el ancho del
     * area: LVGL puede entregar un buffer cuyo stride sea el de la pantalla.
     * Suponer stride == width funcionaba con la pantalla entera (donde
     * coinciden) y desalineaba cada fila en cuanto el area era mas estrecha,
     * que es lo que llenaba la zona de ruido diagonal.
     */
    uint32_t src_stride = (uint32_t)width;
    lv_draw_buf_t *act_buf = lv_display_get_buf_active(disp);
    if (act_buf != NULL && act_buf->header.stride >= 2) {
        src_stride = act_buf->header.stride / 2;   /* bytes -> pixeles RGB565 */
    }

    if (disp_ctx->trans_size) {
        assert(disp_ctx->trans_buf_1 != NULL);

        int x_draw_start = 0;
        int x_draw_end = 0;
        int y_draw_start = 0;
        int y_draw_end = 0;
        int trans_count = 0;

        disp_ctx->trans_act = disp_ctx->trans_buf_1;
        int rotate = disp_ctx->sw_rotate;

        int x_start_tmp = 0;
        int x_end_tmp = 0;
        int max_width = 0;
        int trans_width = 0;

        int y_start_tmp = 0;
        int y_end_tmp = 0;
        int max_height = 0;
        int trans_height = 0;

        if (LV_DISPLAY_ROTATION_270 == rotate || LV_DISPLAY_ROTATION_90 == rotate) {
            max_width = ((disp_ctx->trans_size / height) > width) ? (width) : (disp_ctx->trans_size / height);
            trans_count = width / max_width + (width % max_width ? (1) : (0));

            x_start_tmp = x_start;
            x_end_tmp = x_end;
        } else {
            max_height = ((disp_ctx->trans_size / width) > height) ? (height) : (disp_ctx->trans_size / width);
            trans_count = height / max_height + (height % max_height ? (1) : (0));

            y_start_tmp = y_start;
            y_end_tmp = y_end;
        }

        for (int i = 0; i < trans_count; i++) {

            if (LV_DISPLAY_ROTATION_90 == rotate) {
                trans_width = (x_end - x_start_tmp + 1) > max_width ? max_width : (x_end - x_start_tmp + 1);
                x_end_tmp = (x_end - x_start_tmp + 1) > max_width ? (x_start_tmp + max_width - 1) : x_end;
            } else if (LV_DISPLAY_ROTATION_270 == rotate) {
                trans_width = (x_end_tmp - x_start + 1) > max_width ? max_width : (x_end_tmp - x_start + 1);
                x_start_tmp = (x_end_tmp - x_start + 1) > max_width ? (x_end_tmp - trans_width + 1) : x_start;
            } else if (LV_DISPLAY_ROTATION_0 == rotate) {
                trans_height = (y_end - y_start_tmp + 1) > max_height ? max_height : (y_end - y_start_tmp + 1);
                y_end_tmp = (y_end - y_start_tmp + 1) > max_height ? (y_start_tmp + max_height - 1) : y_end;
            } else {
                trans_height = (y_end_tmp - y_start + 1) > max_height ? max_height : (y_end_tmp - y_start + 1);
                y_start_tmp = (y_end_tmp - y_start + 1) > max_height ? (y_end_tmp - max_height + 1) : y_start;
            }

            disp_ctx->trans_act = (disp_ctx->trans_act == disp_ctx->trans_buf_1) ? (disp_ctx->trans_buf_2) : (disp_ctx->trans_buf_1);
            to = disp_ctx->trans_act;

            /* px_map empieza en la esquina del AREA, no en la de la pantalla:
             * x_start_tmp / y_start_tmp son absolutas y hay que pasarlas a
             * coordenadas relativas al area antes de indexar el buffer. Con la
             * pantalla entera x_start = y_start = 0 y el fallo no se ve; en
             * modo PARTIAL se leia fuera del buffer. */
            const int x_off = x_start_tmp - x_start;
            const int y_off = y_start_tmp - y_start;

            switch (rotate) {
            case LV_DISPLAY_ROTATION_90:
                for (int y = 0; y < height; y++) {
                    for (int x = 0; x < trans_width; x++) {
                        *(to + x * height + (height - y - 1)) = *(from + y * src_stride + x_off + x);
                    }
                }
                x_draw_start = disp_ctx->vres - y_end - 1;
                x_draw_end = disp_ctx->vres - y_start - 1;
                y_draw_start = x_start_tmp;
                y_draw_end = x_end_tmp;
                break;
            case LV_DISPLAY_ROTATION_270:
                for (int y = 0; y < height; y++) {
                    for (int x = 0; x < trans_width; x++) {
                        *(to + (trans_width - x - 1) * height + y) = *(from + y * src_stride + x_off + x);
                    }
                }
                x_draw_start = y_start;
                x_draw_end = y_end;
                y_draw_start = disp_ctx->hres - x_end_tmp - 1;
                y_draw_end = disp_ctx->hres - x_start_tmp - 1;
                break;
            case LV_DISPLAY_ROTATION_180:
                for (int y = 0; y < trans_height; y++) {
                    for (int x = 0; x < width; x++) {
                        *(to + (trans_height - y - 1)*width + (width - x - 1)) = *(from + (y_off + y) * src_stride + x);
                    }
                }
                x_draw_start = disp_ctx->hres - x_end - 1;
                x_draw_end = disp_ctx->hres - x_start - 1;
                y_draw_start = disp_ctx->vres - y_end_tmp - 1;
                y_draw_end = disp_ctx->vres - y_start_tmp - 1;
                break;
            case LV_DISPLAY_ROTATION_0:
                for (int y = 0; y < trans_height; y++) {
                    for (int x = 0; x < width; x++) {
                        *(to + y * (width) + x) = *(from + (y_off + y) * src_stride + x);
                    }
                }
                x_draw_start = x_start;
                x_draw_end = x_end;
                y_draw_start = y_start_tmp;
                y_draw_end = y_end_tmp;
                break;
            default:
                break;
            }

            /* Sincronizacion con el DMA. El codigo original hacia un give y N
             * takes, y lanzaba el ultimo draw_bitmap sin esperarlo antes de
             * dar por terminado el flush: LVGL volvia a pintar el buffer
             * mientras el DMA seguia enviandolo. Con la pantalla entera colaba
             * porque el fotograma siguiente tardaba mas que la transferencia;
             * con areas parciales el render adelanta al DMA y la pantalla se
             * llena de ruido. Ahora el semaforo significa "el DMA esta libre":
             * se coge antes de cada envio y lo devuelve la ISR al terminar. */
            if (0 == i && disp_ctx->draw_wait_cb) {
                disp_ctx->draw_wait_cb(disp_ctx->panel_handle->user_data);
            }

            xSemaphoreTake(disp_ctx->trans_done_sem, portMAX_DELAY);
            esp_lcd_panel_draw_bitmap(disp_ctx->panel_handle, x_draw_start, y_draw_start, x_draw_end + 1, y_draw_end + 1, to);

            if (LV_DISPLAY_ROTATION_90 == rotate) {
                x_start_tmp += max_width;
            } else if (LV_DISPLAY_ROTATION_270 == rotate) {
                x_end_tmp -= max_width;
            } if (LV_DISPLAY_ROTATION_0 == rotate) {
                y_start_tmp += max_height;
            } else {
                y_end_tmp -= max_height;
            }
        }
        /* Esperar a que termine la ULTIMA transferencia antes de devolver el
         * buffer a LVGL, y dejar el semaforo como estaba (DMA libre). */
        xSemaphoreTake(disp_ctx->trans_done_sem, portMAX_DELAY);
        xSemaphoreGive(disp_ctx->trans_done_sem);
    } else {
        esp_lcd_panel_draw_bitmap(disp_ctx->panel_handle, x_start, y_start, x_end + 1, y_end + 1, color_map);
    }
    lv_display_flush_ready(disp);
}

#ifdef ESP_LVGL_PORT_TOUCH_COMPONENT
/* Sin interrupciones durante este tiempo con el dedo supuestamente apoyado, se
 * comprueba una vez si de verdad sigue ahi. Holgado a proposito: el panel
 * interrumpe de sobra durante un arrastre. */
#define TOUCH_STUCK_MS 200

static void lvgl_port_touchpad_read(lv_indev_t *indev, lv_indev_data_t *data)
{
    assert(indev);
    lvgl_port_touch_ctx_t *touch_ctx = (lvgl_port_touch_ctx_t *)lv_indev_get_user_data(indev);
    assert(touch_ctx->handle);

    uint16_t touchpad_x[1] = {0};
    uint16_t touchpad_y[1] = {0};
    uint8_t touchpad_cnt = 0;

    /* Ultimo punto valido. LVGL necesita saber DONDE se solto el dedo, asi que
     * al pasar a RELEASED hay que seguir dando la ultima coordenada buena y no
     * un (0,0). */
    static uint16_t last_x = 0, last_y = 0;
    static lv_indev_state_t last_state = LV_INDEV_STATE_RELEASED;
    static uint32_t last_int_ms = 0;

    /*
     * DESVIACION DEL CODIGO DEL FABRICANTE (a proposito, y medida).
     *
     * El original solo tocaba `data` cuando llegaba una interrupcion. Pero
     * LVGL hace `lv_memzero(data, ...)` ANTES de llamar aqui, y state = 0 es
     * LV_INDEV_STATE_RELEASED: cada lectura sin interrupcion se interpretaba
     * como "dedo levantado". Con LV_DEF_REFR_PERIOD a 16 ms son ~62 lecturas
     * por segundo, muchas mas de las que genera el panel, asi que durante un
     * arrastre LVGL veia PRESSED, RELEASED, PRESSED... y el acumulador del
     * gesto se reiniciaba sin parar.
     *
     * La tentacion es sondear en cada lectura, como hacen otros ports. En
     * ESTE panel eso no vale: medido por Serial, leer sin esperar a la
     * interrupcion devuelve coordenadas rotas. La x se mantenia coherente
     * pero la y saltaba de 23 a 430 en 16 ms, porque se lee mientras el
     * controlador esta escribiendo el reporte. La interrupcion no es un
     * capricho: es la senal de "reporte completo y estable".
     *
     * Asi que se hacen las dos cosas: leer SOLO tras la interrupcion, y
     * conservar el ultimo estado entre interrupciones para no inventar
     * releases. El punto no se queda congelado porque el controlador
     * interrumpe durante todo el arrastre.
     */
    bool touch_int = false;
    if (touch_ctx->touch_wait_cb) {
        touch_int = touch_ctx->touch_wait_cb(touch_ctx->handle->config.user_data);
    }

    if (touch_int) {
        esp_lcd_touch_read_data(touch_ctx->handle);
        bool touchpad_pressed = esp_lcd_touch_get_coordinates(touch_ctx->handle, touchpad_x, touchpad_y, NULL, &touchpad_cnt, 1);

        if (touchpad_pressed && touchpad_cnt > 0) {
            last_x = touchpad_x[0];
            last_y = touchpad_y[0];
            last_state = LV_INDEV_STATE_PRESSED;
        } else {
            last_state = LV_INDEV_STATE_RELEASED;
        }
        last_int_ms = lv_tick_get();
    } else if (last_state == LV_INDEV_STATE_PRESSED &&
               lv_tick_elaps(last_int_ms) > TOUCH_STUCK_MS) {
        /* Red de seguridad: si se perdiera la interrupcion del "dedo fuera",
         * el estado se quedaria pegado en PRESSED para siempre. Pasado el
         * plazo se comprueba UNA vez si sigue habiendo dedo. Aqui la lectura
         * puede venir rota, asi que solo se mira si hay contacto o no; las
         * coordenadas no se tocan. */
        esp_lcd_touch_read_data(touch_ctx->handle);
        uint8_t cnt = 0;
        uint16_t tx[1], ty[1];
        if (!esp_lcd_touch_get_coordinates(touch_ctx->handle, tx, ty, NULL, &cnt, 1) || cnt == 0) {
            last_state = LV_INDEV_STATE_RELEASED;
        }
        last_int_ms = lv_tick_get();
    }

    /* Siempre, pase lo que pase: `data` no puede quedar sin rellenar. */
    data->state   = last_state;
    data->point.x = last_x;
    data->point.y = last_y;

}
#endif

static void lvgl_port_tick_increment(void *arg)
{
    /* Tell LVGL how many milliseconds have elapsed */
    lv_tick_inc(lvgl_port_timer_period_ms);
}

static esp_err_t lvgl_port_tick_init(void)
{
    // Tick interface for LVGL (using esp_timer to generate 2ms periodic event)
    const esp_timer_create_args_t lvgl_tick_timer_args = {
        .callback = &lvgl_port_tick_increment,
        .name = "LVGL tick",
    };
    ESP_RETURN_ON_ERROR(esp_timer_create(&lvgl_tick_timer_args, &lvgl_port_ctx.tick_timer), TAG, "Creating LVGL timer filed!");
    return esp_timer_start_periodic(lvgl_port_ctx.tick_timer, lvgl_port_timer_period_ms * 1000);
}

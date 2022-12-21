#define LV_LVGL_H_INCLUDE_SIMPLE

#include "proxi_calibration_page.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "lvgl_utils.h"
#include "struct.h"
#include "lvgl.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "haptic_lib.h"
#include "pages.h"
#include "Proto.h"

extern int32_t last_distance;

LV_FONT_DECLARE(BahnschriftBase);

extern SemaphoreHandle_t xGuiSemaphore;

extern StructBorders styleBord;
extern StructPage PageInfo;                         //Necessary to know which theme to use

extern StructDeviceSettings dev_settings;

//Style variables for button
lv_style_t styleTxt;
StructColorsBg styleBg;
StructSizeBtn styleSize;
StructTextCol styleTextStruct;

static lv_obj_t * lbl_distance;

static TaskHandle_t proximity_calibration_task = NULL;

static bool selStyle;

#define TAG "PROXICALIBPAGE"

void _lbl_set_text() {
    char buf[127];
    if (last_distance < -1) {
        snprintf(buf, 127, "Waiting for proxy");
    } else {
        if (last_distance == -2) {
            snprintf(buf, 127, "Proxy error");
        } else if (last_distance == -1) {
            snprintf(buf, 127, "NO READING");
        } else if (last_distance < 300) {
            snprintf(buf, 127, "NEAR\nDist: %d", last_distance);
        } else if (last_distance < 500) {
            snprintf(buf, 127, "MEDIUM\nDist: %d", last_distance);
        } else {
            snprintf(buf, 127, "FAR\nDist: %d", last_distance);
        }
        if (pdTRUE == xSemaphoreTake(xGuiSemaphore, pdMS_TO_TICKS(1000))) {
            lv_label_set_text(lbl_distance, buf);
            lv_obj_align(lbl_distance, NULL, LV_ALIGN_CENTER, 0, 0);
            xSemaphoreGive(xGuiSemaphore);
        }
    }
}

static void _proximity_calibration_fsm() {
    while (indexPages == PROXY_CALIBRATION_PAGE) {
        _lbl_set_text();
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    esp_restart();
}

static void _proximity_calibration_fsm_start() {
    if (proximity_calibration_task == NULL) {
        ESP_LOGI("PROXY", "CALIBRATION TASK START");
        xTaskCreate(_proximity_calibration_fsm, "proximity-calibration-fsm", 8000, NULL, 1, &proximity_calibration_task);
    }
}

void proximity_calibration_draw(void) {
    ESP_LOGI(TAG, "PROXYPAGE!");
    _proximity_calibration_fsm_start();
    
    //Creating title label
    lv_obj_t * lbl = lv_label_create(screen, NULL);
    lv_label_set_text(lbl, "PROXY TEST");
    lv_obj_set_style_local_text_font(lbl, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &BahnschriftBase);
    lv_obj_set_style_local_text_color(lbl, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_GRAY);
    lv_obj_align(lbl, NULL, LV_ALIGN_IN_TOP_MID, 0, 20);
    //lv_obj_set_width(lbl, 5);

    //Creating instruction label
    styleBord.pad_top = 2;
    lbl_distance = lv_label_create(screen, NULL);
    _lbl_set_text();
    lv_label_set_align(lbl_distance, LV_LABEL_ALIGN_CENTER);
    lv_obj_set_style_local_text_font(lbl_distance, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &BahnschriftBase);
    lv_obj_set_style_local_text_color(lbl_distance, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_WHITE);
    //lv_obj_set_width(lbl, 10);
}
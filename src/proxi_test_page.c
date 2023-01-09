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

static TaskHandle_t proximity_test_task = NULL;

static bool selStyle;

#define TAG "PROXITESTPAGE"

void _lbl_set_text() {
    char buf[127];
    if (last_distance == -2) {
        snprintf(buf, 127, "ERROR");
    } else if (last_distance == -1) {
        snprintf(buf, 127, "NO READING");
    } else if (last_distance < 200) {
        snprintf(buf, 127, "NEAR\nDist: %d", last_distance);
    } else if (last_distance < 400) {
        snprintf(buf, 127, "MEDIUM\nDist: %d", last_distance);
    } else if (last_distance < 600) {
        snprintf(buf, 127, "FAR\nDist: %d", last_distance);
    } else {
        snprintf(buf, 127, "NO PROXY");
    }
    if (pdTRUE == xSemaphoreTake(xGuiSemaphore, pdMS_TO_TICKS(1000))) {
        lv_label_set_text(lbl_distance, buf);
        lv_obj_align(lbl_distance, NULL, LV_ALIGN_CENTER, 0, 0);
        xSemaphoreGive(xGuiSemaphore);
    }
}

static void _proximity_test_fsm() {
    while (indexPages == PROXY_TEST_PAGE) {
        _lbl_set_text();
        if (ulTaskNotifyTake(pdTRUE, 0)) {
            // Click received
            indexPages = PROXY_CALIBRATION_PAGE;
            recreate_page(false);
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    vTaskDelete(NULL);
}

static void _proximity_test_fsm_start() {
    if (proximity_test_task == NULL) {
        ESP_LOGI("PROXY", "CALIBRATION TEST START");
        xTaskCreate(_proximity_test_fsm, "proximity-calibration-fsm", 8000, NULL, 1, &proximity_test_task);
    }
}

static void _proxytest_cb_event_config(uint8_t row, uint8_t dump, lv_event_t event, uint32_t time_pressed) {
    static bool sent = false;
    switch (event) {
        case LV_EVENT_PRESSED:
            break;

        case LV_EVENT_LONG_PRESSED_REPEAT:
            // Skipping first click after standby
            if (time_pressed >= pdMS_TO_TICKS(buttonMat[indexPages][row].action.timepressed)) {
                if (row == 3 && !sent) {
                    sent = true;
                    xTaskNotifyGive(proximity_test_task);
                }
            }
            break;

        case LV_EVENT_PRESS_LOST:
            break;

        case LV_EVENT_RELEASED:
            // Re enabling click after standby
            if (sent) {
                sent = false;
            }
            break;
    }
}

void proximity_test_draw(void) {
    _proximity_test_fsm_start();
    
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

    // Creating calibration button
    strcpy(buttonMat[indexPages][3].text, "CALIBRATE");
    buttonMat[indexPages][3].btnHeight = BUTTON_APPEARANCE__BUTTON_HEIGHT__BTN_1H;
    buttonMat[indexPages][3].action.timepressed = 500;

    set_style_btn(&styleBg, &styleSize, &styleText, &selStyle, LV_COLOR_BLACK, LV_COLOR_BLACK, LV_COLOR_BLACK, LV_COLOR_BLACK, LV_COLOR_GRAY,
                    LV_COLOR_GRAY, WIDTH_DEFAULT, 0, 0, BUTTON_APPEARANCE__BUTTON_HEIGHT__BTN_1H, 0, X_MARGIN, LV_COLOR_WHITE, LV_COLOR_GREEN, -1);

    StructBorders *border_cnfg_config_page = malloc(sizeof(StructBorders));
    memcpy(border_cnfg_config_page, &PageInfo.info_theme.borders[PageInfo.info_theme.theme_selected], sizeof(PageInfo.info_theme.borders[PageInfo.info_theme.theme_selected]));
    lv_color32_t theme_button_col_border[2];       ///< Main border color
    lv_color32_t theme_button_col_bor_pressed[2];  ///< Border color when pressed
    border_cnfg_config_page->left = 0;
    border_cnfg_config_page->right = 0;
    border_cnfg_config_page->top = 2;
    border_cnfg_config_page->bottom = 0;
    border_cnfg_config_page->theme_button_col_border[0] = LV_COLOR_GRAY;
    border_cnfg_config_page->theme_button_col_border[1] = LV_COLOR_GRAY;

    buttonMat[indexPages][3].rowIdx = 3;
    buttonMat[indexPages][3].colIdx = 1;
    buttonMat[indexPages][3].pointToObject = create_btn(styleSize, screen, &(styleText.font_style), buttonMat[indexPages][3].text, (4 * 10) + 1, styleBg, styleText, &_proxytest_cb_event_config, *border_cnfg_config_page, true, true, false);
    free(border_cnfg_config_page);
}
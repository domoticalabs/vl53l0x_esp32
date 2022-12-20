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

typedef enum {
    PROXI_CALIBR_START,
    PROXI_CALIBR_COUNTDOWN,
    PROXI_CALIBR_RUNNING,
    PROXI_CALIBR_COUNTDOWN_EMPTY,
    PROXI_CALIBR_RUNNING_EMPTY,
    PROXI_CALIBR_END,
    PROXI_CALIBR_FAIL
} proxi_calibration_state_t;

static struct {
    uint8_t countdown;
    proxi_calibration_state_t state;
} proxi_calibration_data;

LV_FONT_DECLARE(BahnschriftBase);

extern SemaphoreHandle_t xGuiSemaphore;

extern StructBorders styleBord;
extern StructPage PageInfo;                         //Necessary to know which theme to use

// Variable to set start of calibration procedure
extern uint8_t do_calibr;
extern uint8_t calibration_abort;

extern bool enable_click;
extern uint8_t oldIndexPag;

extern StructDeviceSettings dev_settings;
extern StructVoltageEnable voltage_en;

//Style variables for button
lv_style_t styleTxt;
StructColorsBg styleBg;
StructSizeBtn styleSize;
StructTextCol styleTextStruct;

static lv_obj_t * lbl_instructions;

static TaskHandle_t proximity_calibration_task = NULL;

static bool selStyle;

#define TAG "PROXICALIBPAGE"

static void _proximity_calibration_fsm() {
    uint32_t start_countdown_time = 0;
    uint8_t changed = false;
    calibration_abort = 0;
    while (indexPages == PROXY_CALIBRATION_PAGE && proxi_calibration_data.state < PROXI_CALIBR_END) {
        ESP_LOGI("PROXY", "CALIBRATION TASK TICK");
        if (proxi_calibration_data.state == PROXI_CALIBR_COUNTDOWN) {
            if (xTaskGetTickCount() - start_countdown_time > pdMS_TO_TICKS(1000)) {
                ESP_LOGI("PROXY", "COUNTDOWN TICK %d", proxi_calibration_data.countdown);
                start_countdown_time = xTaskGetTickCount();
                if (proxi_calibration_data.countdown == 1) {
                    proxi_calibration_data.state = PROXI_CALIBR_RUNNING;
                    do_calibr = 1;
                } else {
                    proxi_calibration_data.countdown--;
                }
                changed = true;
            }
        } else if (proxi_calibration_data.state == PROXI_CALIBR_COUNTDOWN_EMPTY) {
            if (xTaskGetTickCount() - start_countdown_time > pdMS_TO_TICKS(1000)) {
                ESP_LOGI("PROXY", "COUNTDOWN TICK %d", proxi_calibration_data.countdown);
                start_countdown_time = xTaskGetTickCount();
                if (proxi_calibration_data.countdown == 1) {
                    proxi_calibration_data.state = PROXI_CALIBR_RUNNING_EMPTY;
                    do_calibr = 2;
                } else {
                    proxi_calibration_data.countdown--;
                }
                changed = true;
            }
        } else if (proxi_calibration_data.state == PROXI_CALIBR_RUNNING) {
            if (do_calibr == 0) {
                ESP_LOGI("PROXY", "Calibration END");
                proxi_calibration_data.state = PROXI_CALIBR_COUNTDOWN_EMPTY;
                proxi_calibration_data.countdown = 5;
                changed = true;
            } else if (do_calibr != 1) {
                ESP_LOGI("PROXY", "Calibration END");
                proxi_calibration_data.state = PROXI_CALIBR_FAIL;
                do_calibr = 0;
                changed = true;
            }
        } else if (proxi_calibration_data.state == PROXI_CALIBR_RUNNING_EMPTY) {
            if (do_calibr == 0) {
                ESP_LOGI("PROXY", "Calibration END");
                proxi_calibration_data.state = PROXI_CALIBR_END;
                changed = true;
            } else if (do_calibr != 2) {
                ESP_LOGI("PROXY", "Calibration END");
                proxi_calibration_data.state = PROXI_CALIBR_FAIL;
                do_calibr = 0;
                changed = true;
            }
        }
        if (ulTaskNotifyTake(pdTRUE, 0)) {
            // Click received
            ESP_LOGI("PROXY", "CLICK RECEIVED");
            switch (proxi_calibration_data.state)
            {
                case PROXI_CALIBR_START:
                    proxi_calibration_data.countdown = 5;
                    start_countdown_time = xTaskGetTickCount();
                    proxi_calibration_data.state = PROXI_CALIBR_COUNTDOWN;
                    changed = true;
                    break;

                default:
                    calibration_abort = 1;
                    proxi_calibration_data.countdown = 0;
                    proxi_calibration_data.state = PROXI_CALIBR_FAIL;
                    changed = true;
                    break;
            }
        }
        if (changed) {
            lv_obj_t *bg = NULL;
            recreate_page(false);
            changed = false;
        }
        ESP_LOGI("PROXY", "TASK TICK ENDD");
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    ESP_LOGI("PROXY", "CALIBRATION TASK END");
    proximity_calibration_task = NULL;
    if (indexPages == PROXY_CALIBRATION_PAGE) {
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
    if (proxi_calibration_data.state == PROXI_CALIBR_END) {
        esp_restart();
    }
    vTaskDelete(NULL);
}

static void _proximity_calibration_fsm_start() {
    if (proximity_calibration_task == NULL) {
        ESP_LOGI("PROXY", "CALIBRATION TASK START");
        xTaskCreate(_proximity_calibration_fsm, "proximity-calibration-fsm", 8000, NULL, 1, &proximity_calibration_task);
    }
}

static void _configurationpage_cb_event_config (uint8_t row, uint8_t dump, lv_event_t event, uint32_t time_pressed) {
    static bool sent = false;
    switch (event)
    {
        case LV_EVENT_PRESSED:
        break;

        case LV_EVENT_LONG_PRESSED_REPEAT:
            //Skipping first click after standby
            if (!enable_click)
            {
                break;
            }
            if (time_pressed >= pdMS_TO_TICKS(buttonMat[indexPages][row].action.timepressed))
            {
                if (row == 3 && !sent)
                {
                    sent = true;
                    if (dev_settings.physicall.hapticenable == true && voltage_en.haptic_desabled == false)
                    {
                        haptic_lclick(100);
                    }
                    if (proximity_calibration_task == NULL) {
                         _proximity_calibration_fsm_start();
                    }
                    xTaskNotifyGive(proximity_calibration_task);
                }
            }
        break;

        case LV_EVENT_PRESS_LOST:
        break;

        case LV_EVENT_RELEASED:
            //Re enabling click after standby
            if (!enable_click)
            {
                enable_click = true;
                break;
            }
            if (sent)
            {
                sent = false;
            }
        break;
    }
}

void proximity_calibration_draw(void) {
    buttonMat[indexPages][3].behavior.behavior = BUTTON_BEHAVIOR__BUTTON_BEHAVIOR_TYPE__STATIC_BTN_BG;
    buttonMat[indexPages][3].icon = -1;
    buttonMat[indexPages][3].colors[0] = LV_COLOR_WHITE;
    
    //Creating title label
    lv_obj_t * lbl = lv_label_create(screen, NULL);
    lv_label_set_text(lbl, "CALIBRATION");
    lv_obj_set_style_local_text_font(lbl, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &BahnschriftBase);
    lv_obj_set_style_local_text_color(lbl, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, lv_color_darken(PageInfo.font_color[PageInfo.info_theme.theme_selected][0], 50));
    lv_obj_align(lbl, NULL, LV_ALIGN_IN_TOP_MID, 0, 20);
    //lv_obj_set_width(lbl, 5);

    //Creating instruction label
    styleBord.pad_top = 2;
    lbl_instructions = lv_label_create(screen, NULL);
    char buf [127];
    switch (proxi_calibration_data.state)
    {
        case PROXI_CALIBR_START:
            snprintf(buf, 127, "Please press\nthe calibration\nbutton below");
            break;

        case PROXI_CALIBR_COUNTDOWN:
            snprintf(buf, 127, "Stand still\nat 60cm\nfrom the device\n\nCalibration will\nstart in %d", proxi_calibration_data.countdown);
            break;

        case PROXI_CALIBR_COUNTDOWN_EMPTY:
            snprintf(buf, 127, "Stand still\nat 100cm\nfrom the device\n\nCalibration will\nstart in %d", proxi_calibration_data.countdown);
            break;

        case PROXI_CALIBR_RUNNING:
            snprintf(buf, 127, "Device is\ncalibrating\n\nPlease stand\nstill");
            break;

        case PROXI_CALIBR_RUNNING_EMPTY:
            snprintf(buf, 127, "Device is\ncalibrating\n\nPlease wait");
            break;

        case PROXI_CALIBR_END:
            snprintf(buf, 127, "Calibration\ncompleted\n\nYou can now\nexit this page");
            break;

        default:
            snprintf(buf, 127, "Calibration failed\n\nPlase enter\nagain in\nthis page");
            break;
    }
    lv_label_set_text(lbl_instructions, buf);
    lv_label_set_align(lbl_instructions, LV_LABEL_ALIGN_CENTER);
    lv_obj_set_style_local_text_font(lbl_instructions, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &BahnschriftBase);
    lv_obj_set_style_local_text_color(lbl_instructions, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, PageInfo.font_color[PageInfo.info_theme.theme_selected][0]);
    lv_obj_align(lbl_instructions, NULL, LV_ALIGN_IN_TOP_MID, 0, 55);
    //lv_obj_set_width(lbl, 10);
    

    //Creating calibration button
    if (proxi_calibration_data.state < PROXI_CALIBR_END) {
        strcpy(buttonMat[indexPages][3].text, (proxi_calibration_data.state == PROXI_CALIBR_START) ? "CALIBRATE" : "STOP");
        buttonMat[indexPages][3].btnHeight = BUTTON_APPEARANCE__BUTTON_HEIGHT__BTN_1H;
        buttonMat[indexPages][3].action.timepressed = 500;

        set_style_btn(&styleBg, &styleSize, &styleText, &selStyle, buttonMat[indexPages][3].color_border_shadow[0], buttonMat[indexPages][3].color_border_shadow[1], buttonMat[indexPages][3].color_bg_shadow[0], buttonMat[indexPages][3].color_bg_shadow[1], PageInfo.info_theme.theme_col_btn_pressed[PageInfo.info_theme.theme_selected][0],
                                    PageInfo.info_theme.theme_col_btn_pressed[PageInfo.info_theme.theme_selected][1], WIDTH_DEFAULT, PageInfo.info_theme.borders[PageInfo.info_theme.theme_selected].pad_top, PageInfo.info_theme.borders[PageInfo.info_theme.theme_selected].pad_left, buttonMat[indexPages][3].btnHeight, PageInfo.info_theme.separetor[PageInfo.info_theme.theme_selected].height, X_MARGIN, PageInfo.font_color[PageInfo.info_theme.theme_selected][0], PageInfo.font_color[PageInfo.info_theme.theme_selected][1], buttonMat[indexPages][3].icon);

        StructBorders *border_cnfg_config_page = malloc(sizeof(StructBorders));
        memcpy(border_cnfg_config_page, &PageInfo.info_theme.borders[PageInfo.info_theme.theme_selected], sizeof(PageInfo.info_theme.borders[PageInfo.info_theme.theme_selected]));
        border_cnfg_config_page->theme_button_col_border[0] = buttonMat[indexPages][3].color_border_shadow[0];
        border_cnfg_config_page->theme_button_col_border[1] = buttonMat[indexPages][3].color_border_shadow[1];
        
        buttonMat[indexPages][3].rowIdx = 3;
        buttonMat[indexPages][3].colIdx = 1;
        buttonMat[indexPages][3].pointToObject = create_btn(styleSize, screen, &(styleText.font_style), buttonMat[indexPages][3].text, (4*10) + 1, styleBg, styleText, &_configurationpage_cb_event_config, *border_cnfg_config_page, true, true, false);
        free (border_cnfg_config_page);
    }
}
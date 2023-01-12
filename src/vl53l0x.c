/*
 * File : vl53l0x.c
 * Created: Thursday, 04 February 2021
 * Author: yunsik oh (oyster90@naver.com)
 * 
 * Modified: Thursday, 04 February 2021
 * 
 */
#include "vl53l0x.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "i2c_mux.h"
#include "nvs_flash.h"
#include "struct.h"
#include "vl53l0x_platform_log.h"

static float sensibility;

#define VERSION_REQUIRED_MAJOR 1
#define VERSION_REQUIRED_MINOR 0
#define VERSION_REQUIRED_BUILD 2

#define N_READINGS 3
#define XTALK_END 5400
#define XTALK_START 25
#define XTALK_STEP 25
#define XTALK_TARGET_DST 600

#define IO_NUM          GPIO_NUM_5
#define IO_SEL(num)     (1ULL << (num))

#define msec(t) ((t) / portTICK_PERIOD_MS) // millisecond convert
#define sec(t) ((t) * 100U)
#define minute(t) (sec(t * 60));

static const char* TAG = "vl53l0x";
#define VL53L0X_TAG TAG

#ifdef VL53L0X_LOG_ENABLE
#define VL53L0X_Log(level, fmt, ...) \
    ESP_LOG_LEVEL_LOCAL(level, VL53L0X_TAG, fmt, ##__VA_ARGS__)

#define VL53L0X_ErrLog(fmt, ...) \
    VL53L0X_Log(ESP_LOG_ERROR, "VL53L0X_ErrLog %s" fmt, __func__, ##__VA_ARGS__)
#else
    #define VL53L0X_Log(level, fmt, ...) (void)0
    #define VL53L0X_ErrLog(fmt, ...) (void)0
#endif

extern StructDeviceSettings dev_settings;

static void print_pal_error(VL53L0X_Error Status)
{
    char buf[VL53L0X_MAX_STRING_LENGTH];
    VL53L0X_GetPalErrorString(Status, buf);
    VL53L0X_ErrLog("API Status: %i : %s\n", Status, buf);
}

static VL53L0X_Error WaitMeasurementDataReady(VL53L0X_DEV Dev)
{
    VL53L0X_Error Status = VL53L0X_ERROR_NONE;
    uint8_t NewDatReady = 0;
    uint32_t LoopNb;

    // Wait until it finished
    // use timeout to avoid deadlock
    if (Status == VL53L0X_ERROR_NONE)
    {
        LoopNb = 0;
        do
        {
            Status = VL53L0X_GetMeasurementDataReady(Dev, &NewDatReady);
            if ((NewDatReady == 0x01) || Status != VL53L0X_ERROR_NONE)
            {
                break;
            }
            LoopNb = LoopNb + 1;
            VL53L0X_PollingDelay(Dev);
        } while (LoopNb < VL53L0X_DEFAULT_MAX_LOOP);

        if (LoopNb >= VL53L0X_DEFAULT_MAX_LOOP)
        {
            Status = VL53L0X_ERROR_TIME_OUT;
        }
    }

    return Status;
}

static VL53L0X_Error WaitStopCompleted(VL53L0X_DEV Dev)
{
    VL53L0X_Error Status = VL53L0X_ERROR_NONE;
    uint32_t StopCompleted = 0;
    uint32_t LoopNb;

    // Wait until it finished
    // use timeout to avoid deadlock
    if (Status == VL53L0X_ERROR_NONE)
    {
        LoopNb = 0;
        do
        {
            Status = VL53L0X_GetStopCompletedStatus(Dev, &StopCompleted);
            if ((StopCompleted == 0x00) || Status != VL53L0X_ERROR_NONE)
            {
                break;
            }
            LoopNb = LoopNb + 1;
            VL53L0X_PollingDelay(Dev);
        } while (LoopNb < VL53L0X_DEFAULT_MAX_LOOP);

        if (LoopNb >= VL53L0X_DEFAULT_MAX_LOOP)
        {
            Status = VL53L0X_ERROR_TIME_OUT;
        }
    }

    return Status;
}

VL53L0X_Error _VL53L0X_Device_init(VL53L0X_Dev_t *device, uint32_t *xtalk, uint8_t config)
{
    static uint32_t refSpadCount = 0;
    static uint8_t isApertureSpads = 0;
    static uint8_t VhvSettings = 0;
    static uint8_t PhaseCal = 0;
    VL53L0X_Error Status = VL53L0X_ERROR_NONE;
    VL53L0X_Dev_t *pMyDevice = device;
    VL53L0X_Version_t Version;
    VL53L0X_Version_t *pVersion = &Version;
    VL53L0X_DeviceInfo_t DeviceInfo;

    esp_log_level_set(TAG, ESP_LOG_INFO);

    ESP_LOGI("PROXY", "Applying XTALK: %u, Sens: %f", *xtalk, sensibility);

    pMyDevice->comms_type = 1;
    pMyDevice->comms_speed_khz = I2C_MUX_BAUDRATE/1000;
    pMyDevice->I2cDevAddr = CONFIG_VL53L0X_I2C_ADDR;

    Status = VL53L0X_comms_initialise(0, I2C_MUX_BAUDRATE/1000);
    if (Status != VL53L0X_ERROR_NONE)
    {
        VL53L0X_ErrLog("i2c init failed!");
        return Status;
    }

    /*
     *  Get the version of the VL53L0X API running in the firmware
     */

    int32_t status_int;
    status_int = VL53L0X_GetVersion(pVersion);
    if (status_int != 0)
        Status = VL53L0X_ERROR_CONTROL_INTERFACE;

    /*
     *  Verify the version of the VL53L0X API running in the firmrware
     */

    if (Status == VL53L0X_ERROR_NONE)
    {
        if (pVersion->major != VERSION_REQUIRED_MAJOR ||
            pVersion->minor != VERSION_REQUIRED_MINOR ||
            pVersion->build != VERSION_REQUIRED_BUILD)
        {
            VL53L0X_Log(ESP_LOG_DEBUG, "VL53L0X API Version Error: Your firmware has %d.%d.%d (revision %d). This example requires %d.%d.%d.\n",
                     pVersion->major, pVersion->minor, pVersion->build, pVersion->revision,
                     VERSION_REQUIRED_MAJOR, VERSION_REQUIRED_MINOR, VERSION_REQUIRED_BUILD);
        }
    }

    // End of implementation specific
    VL53L0X_Log(ESP_LOG_DEBUG, "Call of VL53L0X_DataInit\n");
    Status = VL53L0X_DataInit(pMyDevice); // Data initialization
    if (Status != VL53L0X_ERROR_NONE)
    {
        print_pal_error(Status);
        return Status;
    }

    Status = VL53L0X_GetDeviceInfo(pMyDevice, &DeviceInfo);
    if (Status != VL53L0X_ERROR_NONE)
    {
        print_pal_error(Status);
        return Status;
    }

    VL53L0X_Log(ESP_LOG_DEBUG, "VL53L0X_GetDeviceInfo:\n");
    VL53L0X_Log(ESP_LOG_DEBUG, "Device Name : %s\n", DeviceInfo.Name);
    VL53L0X_Log(ESP_LOG_DEBUG, "Device Type : %s\n", DeviceInfo.Type);
    VL53L0X_Log(ESP_LOG_DEBUG, "Device ID : %s\n", DeviceInfo.ProductId);
    VL53L0X_Log(ESP_LOG_DEBUG, "ProductRevisionMajor : %d\n", DeviceInfo.ProductRevisionMajor);
    VL53L0X_Log(ESP_LOG_DEBUG, "ProductRevisionMinor : %d\n", DeviceInfo.ProductRevisionMinor);

    if ((DeviceInfo.ProductRevisionMinor != 1) && (DeviceInfo.ProductRevisionMinor != 1))
    {
        VL53L0X_Log(ESP_LOG_DEBUG, "Error expected cut 1.1 but found cut %d.%d\n",
                    DeviceInfo.ProductRevisionMajor, DeviceInfo.ProductRevisionMinor);
        Status = VL53L0X_ERROR_NOT_SUPPORTED;
        return Status;
    }

    // StaticInit will set interrupt by default
    VL53L0X_Log(ESP_LOG_DEBUG, "Call of VL53L0X_StaticInit\n");
    Status = VL53L0X_StaticInit(pMyDevice); // Device Initialization

    if (Status != VL53L0X_ERROR_NONE)
    {
        print_pal_error(Status);
        return Status;
    }

    ESP_LOGI(TAG, "Calibrating SPAD...");

    //================================
    // RefCalibration Data Handling
    //================================

    if (refSpadCount == 0) {
        VL53L0X_Log(ESP_LOG_DEBUG, "Call of VL53L0X_PerformRefSpadManagement\n");
        Status = VL53L0X_PerformRefSpadManagement(pMyDevice,
                                                &refSpadCount, &isApertureSpads);  // Device Initialization
        if (Status != VL53L0X_ERROR_NONE) {
            print_pal_error(Status);
            return Status;
        }

        ESP_LOGI(TAG, "SPAD: %d, %u", refSpadCount, isApertureSpads);

        ESP_LOGI(TAG, "Calibrating Ref...");

        Status = VL53L0X_PerformRefCalibration(pMyDevice,
                                            &VhvSettings, &PhaseCal);  // Device Initialization
        if (Status != VL53L0X_ERROR_NONE) {
            print_pal_error(Status);
            return Status;
        }
        ESP_LOGI(TAG, "REF: %u, %u", VhvSettings, PhaseCal);
    } else {
        VL53L0X_SetReferenceSpads(pMyDevice, refSpadCount, isApertureSpads);  // Device Initialization
        VL53L0X_SetRefCalibration(pMyDevice, VhvSettings, PhaseCal);  // Device Initialization
    }

    int32_t pOffsetMicroMeter = 10800;

    VL53L0X_SetOffsetCalibrationDataMicroMeter(pMyDevice, pOffsetMicroMeter);

    if (config == 2) {
        VL53L0X_PerformXTalkCalibration(pMyDevice, XTALK_TARGET_DST << 16, xtalk);
        VL53L0X_SetXTalkCompensationRateMegaCps(pMyDevice, xtalk);
        VL53L0X_SetXTalkCompensationEnable(pMyDevice, 1);
    } else if (*xtalk > 0) {
        FixPoint1616_t pXTalkCompensationRateMegaCps = *xtalk;
        VL53L0X_SetXTalkCompensationRateMegaCps(pMyDevice, pXTalkCompensationRateMegaCps);
        VL53L0X_SetXTalkCompensationEnable(pMyDevice, 1);
    }

    // SET PROFILE
    /*Status = VL53L0X_SetLimitCheckValue(pMyDevice, VL53L0X_CHECKENABLE_SIGNAL_RATE_FINAL_RANGE, (FixPoint1616_t)(0.25 * 65536));
    Status = VL53L0X_SetLimitCheckValue(pMyDevice, VL53L0X_CHECKENABLE_SIGMA_FINAL_RANGE, (FixPoint1616_t)(18 * 65536));
    Status = VL53L0X_SetMeasurementTimingBudgetMicroSeconds(pMyDevice, 200000);*/
    
    Status = VL53L0X_SetLimitCheckValue(pMyDevice, VL53L0X_CHECKENABLE_SIGNAL_RATE_FINAL_RANGE, (FixPoint1616_t)(0.1 * 65536));
    Status = VL53L0X_SetLimitCheckValue(pMyDevice, VL53L0X_CHECKENABLE_SIGMA_FINAL_RANGE, (FixPoint1616_t)(60 * 65536));
    Status = VL53L0X_SetMeasurementTimingBudgetMicroSeconds(pMyDevice, (config != 0) ? 33000: 200000);
    Status = VL53L0X_SetVcselPulsePeriod(pMyDevice, VL53L0X_VCSEL_PERIOD_PRE_RANGE, 18);
    Status = VL53L0X_SetVcselPulsePeriod(pMyDevice, VL53L0X_VCSEL_PERIOD_FINAL_RANGE, 14);

    VL53L0X_Log(ESP_LOG_DEBUG, "Call of VL53L0X_SetDeviceMode\n");
    VL53L0X_DeviceModes default_device_mode = VL53L0X_DEVICEMODE_CONTINUOUS_RANGING;
    Status = VL53L0X_SetDeviceMode(pMyDevice, default_device_mode); // Setup in single ranging mode
    if (Status != VL53L0X_ERROR_NONE)
    {
        print_pal_error(Status);
        return Status;
    }

    VL53L0X_Log(ESP_LOG_DEBUG, "Call of VL53L0X_StartMeasurement\n");
    Status = VL53L0X_StartMeasurement(pMyDevice);
    if (Status != VL53L0X_ERROR_NONE)
    {
        print_pal_error(Status);
        return Status;
    }

    VL53L0X_PollingDelay(pMyDevice);
    ESP_LOGI("PROXY", "SENS: %d", dev_settings.proximity_config.sensitivity);
    return Status;
}

#define CALIBR_DEF 50
#define SENS_DEF 5000
VL53L0X_Error VL53L0X_Device_init(VL53L0X_Dev_t *device) {
    uint32_t calibr = CALIBR_DEF;
    uint32_t temp = SENS_DEF;

    // Read calibration from NVS
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("proxy", NVS_READWRITE, &nvs_handle);
    if (err == ESP_OK) {
        err = nvs_get_u32(nvs_handle, "calibr", &calibr);
        if (err != ESP_OK) {
            calibr = CALIBR_DEF;
        }
        err = nvs_get_u32(nvs_handle, "sens", &temp);
        if (err != ESP_OK) {
            temp = SENS_DEF;
        }
        nvs_close(nvs_handle);
    }
    sensibility = ((float) temp) / 100.0;

    return _VL53L0X_Device_init(device, &calibr, 0);
}

uint8_t calibration_abort;
VL53L0X_Error VL53L0X_Device_calibration(VL53L0X_Dev_t *device, uint8_t empty) {
    uint32_t xtalk;
    VL53L0X_Error error;
    uint32_t best, best_distance = 0xFFFFFFFF, actual_best_distance, step = 0, start = 0, end = 0;
    uint8_t i = 0, found = 0, counter = 0, status = 0;
    float sens, best_sens = 0;
    VL53L0X_RangingMeasurementData_t RangingMeasurementData;

    ESP_LOGI("PROXY", "Device deinit %d", VL53L0X_Device_deinit(device));

    if (!empty) {
        for (xtalk = XTALK_START; !calibration_abort && !found && xtalk < XTALK_END; xtalk += XTALK_STEP) {
            ESP_LOGI("PROXY", "xTalk: %u", xtalk);
            if ((error = _VL53L0X_Device_init(device, &xtalk, 1)) != VL53L0X_ERROR_NONE) {
                ESP_LOGE("PROXY", "Error %d", error);
                return error;
            }

            for (i = 0; i < N_READINGS; i++) {
                error = WaitMeasurementDataReady(device);
                error |= VL53L0X_GetRangingMeasurementData(device, &RangingMeasurementData);
                // Clear the interrupt
                VL53L0X_ClearInterruptMask(device, VL53L0X_REG_SYSTEM_INTERRUPT_GPIO_NEW_SAMPLE_READY);
                if (error != VL53L0X_ERROR_TIME_OUT && error != VL53L0X_ERROR_CONTROL_INTERFACE) {
                    actual_best_distance = XTALK_TARGET_DST - RangingMeasurementData.RangeMilliMeter;
                    if (actual_best_distance < 0) {
                        actual_best_distance *= -1;
                    }
                    ESP_LOGI("PROXY", "Actual: %u, Best: %u", actual_best_distance, best_distance);
                    if (actual_best_distance < best_distance) {
                        status = 1;
                        counter = 0;
                        best_distance = actual_best_distance;
                        best = xtalk;
                        if (best_distance < 50) {
                            found = 1;
                        }
                    } else if (status == 1 && best_distance < 300) {
                        if (actual_best_distance - best_distance > 100) {
                            if (counter++ > (best_distance>>6)*N_READINGS) {
                                found = 1;
                            }
                        }
                    }
                }
                vTaskDelay(pdMS_TO_TICKS(20));
            }
            VL53L0X_Device_deinit(device);
            vTaskDelay(pdMS_TO_TICKS(20));
        }
        if (!calibration_abort && best_distance < 300) {
            ESP_LOGI("PROXY", "Best %u", best);
            // Save in NVS
            nvs_handle_t nvs_handle;
            esp_err_t err = nvs_open("proxy", NVS_READWRITE, &nvs_handle);
            if (err == ESP_OK) {
                err = nvs_set_u32(nvs_handle, "calibr", best);
                nvs_commit(nvs_handle);
                nvs_close(nvs_handle);
            }

            return VL53L0X_ERROR_NONE;
        }

        calibration_abort = 0;
        return VL53L0X_ERROR_CALIBRATION_WARNING;
    } else {
        nvs_handle_t nvs_handle;
        esp_err_t err = nvs_open("proxy", NVS_READWRITE, &nvs_handle);
        if (err == ESP_OK) {
            err = nvs_get_u32(nvs_handle, "calibr", &xtalk);
            nvs_close(nvs_handle);
        }
        if ((error = _VL53L0X_Device_init(device, &xtalk, 0)) != VL53L0X_ERROR_NONE) {
            ESP_LOGE("PROXY", "Error %d", error);
            return error;
        }

        for (i = 0; i < N_READINGS; i++) {
            error = WaitMeasurementDataReady(device);
            error |= VL53L0X_GetRangingMeasurementData(device, &RangingMeasurementData);
            // Clear the interrupt
            VL53L0X_ClearInterruptMask(device, VL53L0X_REG_SYSTEM_INTERRUPT_GPIO_NEW_SAMPLE_READY);
            if (error != VL53L0X_ERROR_TIME_OUT && error != VL53L0X_ERROR_CONTROL_INTERFACE) {
                sens = ((float)RangingMeasurementData.SignalRateRtnMegaCps) / ((float)RangingMeasurementData.AmbientRateRtnMegaCps);
                ESP_LOGI("PROXY", "Sens %f", sens);
                if (sens > best_sens) {
                    best_sens = sens;
                }
            }
            vTaskDelay(pdMS_TO_TICKS(20));
        }
        ESP_LOGI("PROXY", "Best %f", best_sens);
        if (!calibration_abort) {            
            // Save in NVS
            nvs_handle_t nvs_handle;
            esp_err_t err = nvs_open("proxy", NVS_READWRITE, &nvs_handle);
            if (err == ESP_OK) {
                    err = nvs_set_u32(nvs_handle, "sens", (uint32_t) (best_sens * 120.0));
                    nvs_commit(nvs_handle);
                    nvs_close(nvs_handle);
            }
            return VL53L0X_ERROR_NONE;
        }
        calibration_abort = 0;
        return VL53L0X_ERROR_CALIBRATION_WARNING;
    }
}

VL53L0X_Error VL53L0X_Device_deinit(VL53L0X_Dev_t *device)
{
    VL53L0X_Error Status;

    VL53L0X_Log(ESP_LOG_DEBUG, "Call of VL53L0X_StopMeasurement\n");
    Status = VL53L0X_StopMeasurement(device);
    if (Status != VL53L0X_ERROR_NONE)
    {
        print_pal_error(Status);
        return Status;
    }

    VL53L0X_Log(ESP_LOG_DEBUG, "Wait Stop to be competed\n");
    Status = WaitStopCompleted(device);
    if (Status != VL53L0X_ERROR_NONE)
    {
        print_pal_error(Status);
        return Status;
    }

    Status = VL53L0X_ClearInterruptMask(device,
                                        VL53L0X_REG_SYSTEM_INTERRUPT_GPIO_NEW_SAMPLE_READY);
    if (Status != VL53L0X_ERROR_NONE)
    {
        print_pal_error(Status);
        return Status;
    }

    return Status;
}

#define SENS_HIGH   17500000    // 100cm
#define SENS_MED    43500000    // 80cm
#define SENS_LOW    116000000   // 50cm
inline bool filter(VL53L0X_RangingMeasurementData_t *RangingMeasurementData) {
    float sens;
    if (RangingMeasurementData->RangeStatus != 0)
        return false;
    sens = ((float)RangingMeasurementData->SignalRateRtnMegaCps) / ((float)RangingMeasurementData->AmbientRateRtnMegaCps);
    ESP_LOGI("PROXY", "%d;%d;%d;%.3f", RangingMeasurementData->RangeMilliMeter, RangingMeasurementData->SignalRateRtnMegaCps, RangingMeasurementData->AmbientRateRtnMegaCps, sens);
    if (sens > sensibility && RangingMeasurementData->RangeMilliMeter < 800) {
        return true;
    }
    return false;
}

VL53L0X_Error VL53L0X_Device_getMeasurement(VL53L0X_Dev_t *device, uint16_t* data)
{
    VL53L0X_Error Status;
    Status = WaitMeasurementDataReady(device);
    if (Status != VL53L0X_ERROR_NONE)
    {
        VL53L0X_ErrLog("WaitMeasurementDataReady error (%d)", Status);
        return Status;
    }

    VL53L0X_RangingMeasurementData_t RangingMeasurementData;
    Status = VL53L0X_GetRangingMeasurementData(device, &RangingMeasurementData);
    ESP_LOGI("PROXY", "Get measure %d", Status);
    if (Status != VL53L0X_ERROR_NONE)
    {
        VL53L0X_ErrLog("VL53L0X_GetRangingMeasurementData error (%d)", Status);
        return Status;
    }

    // Clear the interrupt
    VL53L0X_ClearInterruptMask(device, VL53L0X_REG_SYSTEM_INTERRUPT_GPIO_NEW_SAMPLE_READY);

    //if (RangingMeasurementData.RangeStatus == 0 && ((RangingMeasurementData.RangeMilliMeter < 50 && RangingMeasurementData.SignalRateRtnMegaCps >> 16 >= 3) || (RangingMeasurementData.RangeMilliMeter >= 50 && RangingMeasurementData.SignalRateRtnMegaCps >> 16 >= 2)))
    if (filter(&RangingMeasurementData))
    {
        *data = RangingMeasurementData.RangeMilliMeter;
        return Status;
    }

    return VL53L0X_ERROR_UNDEFINED;
}

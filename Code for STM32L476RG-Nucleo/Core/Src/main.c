/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "adc.h"
#include "i2c.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "oled.h"
#include "max30102.h"
#include "dht11.h"
#include "DS3231.h"
#include "IIC.h"
#include "mpu6050.h"
#include "inv_mpu.h"
#include "inv_mpu_dmp_motion_driver.h"

/*
 * Application overview:
 * - polls several sensors on a simple cooperative schedule,
 * - keeps the latest results in module-specific state structures,
 * - mirrors the current page to the OLED,
 * - and periodically prints a consolidated telemetry packet over Bluetooth/UART.
 */
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
/* Cached DS3231 time after any display-side offset has been applied. */
typedef struct
{
    uint8_t year;
    uint8_t month;
    uint8_t day;
    uint8_t weekday;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint8_t valid;
} rtc_state_t;

/* Latest DHT11 reading. */
typedef struct
{
    uint8_t temp;
    uint8_t humidity;
    uint8_t valid;
} dht_state_t;

/* Raw ADC light reading plus a normalized percentage for display/telemetry. */
typedef struct
{
    uint32_t raw;
    uint8_t percent;
} light_state_t;

/* Motion snapshot combining orientation, raw IMU values, and the software step counter. */
typedef struct
{
    float pitch;
    float roll;
    float yaw;
    int16_t ax;
    int16_t ay;
    int16_t az;
    int16_t gx;
    int16_t gy;
    int16_t gz;
    uint32_t steps;
    uint8_t valid;
} motion_state_t;

/* Smoothed pulse oximeter result plus validity/finger-detection flags. */
typedef struct
{
    uint16_t heart_rate;
    uint16_t spo2;
    uint8_t hr_valid;
    uint8_t spo2_valid;
    uint8_t finger_present;
} pulse_state_t;

/* Position fix cached from either the hardware GNSS receiver or Bluetooth-relayed coordinates. */
typedef struct
{
    char utc[16];
    char date[8];
    char lat[16];
    char lon[16];
    char lat_deg[16];
    char lon_deg[16];
    char lat_hemi;
    char lon_hemi;
    uint8_t fix_valid;
    uint32_t last_update_ms;
} gps_state_t;

/* UI-only state for page selection and alarm thresholds. */
typedef struct
{
    uint8_t page;
    uint8_t temp_warn;
    uint16_t hr_warn;
} ui_state_t;
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define MENU_PAGE_COUNT                 6U
#define OLED_TEXT_COLS                  21U

/*
 * LM393 AO on most photoresistor modules rises when ambient light gets stronger.
 * If your board behaves in the opposite direction, change this define to 0.
 */
#define LIGHT_SENSOR_HIGH_WHEN_BRIGHT   0U
#define RTC_DISPLAY_OFFSET_SECONDS      10

/* Cooperative scheduler periods for the main loop. */
#define KEY_SCAN_MS                     20U
#define MPU_UPDATE_MS                   40U
#define ADC_UPDATE_MS                   100U
#define RTC_UPDATE_MS                   250U
#define DHT_UPDATE_MS                   2000U
#define MAX30102_UPDATE_MS              100U
#define MAX30102_STARTUP_DELAY_MS       100U
#define OLED_UPDATE_MS                  200U
#define BT_UPDATE_MS                    1000U
#define GPS_STALE_MS                    10000U

#define TEMP_WARN_DEFAULT               30U
#define TEMP_WARN_MIN                   0U
#define TEMP_WARN_MAX                   60U

#define HR_WARN_DEFAULT                 120U
#define HR_WARN_MIN                     60U
#define HR_WARN_MAX                     200U

#define HR_FILTER_DEPTH                 7U
#define MAX30102_INVALID_HOLD           2U
#define GPS_LINE_MAX                    96U
/* USER CODE END PD */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
/* Latest module snapshots used by the OLED page renderer and Bluetooth output. */
static rtc_state_t g_rtc = {0};
static dht_state_t g_dht = {0};
static light_state_t g_light = {0};
static motion_state_t g_motion = {0};
static pulse_state_t g_pulse = {0};
static gps_state_t g_gps = {{0}};
static gps_state_t g_bt_gps = {{0}};
static ui_state_t g_ui = {0, TEMP_WARN_DEFAULT, HR_WARN_DEFAULT};

/* Line-assembly state for byte-wise UART reception. */
static uint8_t g_gps_rx_byte = 0U;
static uint8_t g_bt_rx_byte = 0U;
static volatile uint16_t g_gps_line_index = 0U;
static volatile uint8_t g_gps_sentence_ready = 0U;
static char g_gps_line_buffer[GPS_LINE_MAX] = {0};
static char g_gps_sentence_buffer[GPS_LINE_MAX] = {0};

static volatile uint16_t g_bt_line_index = 0U;
static volatile uint8_t g_bt_sentence_ready = 0U;
static char g_bt_line_buffer[GPS_LINE_MAX] = {0};
static char g_bt_sentence_buffer[GPS_LINE_MAX] = {0};

/* Cached OLED rows so only changed lines are rewritten, which reduces visible flicker. */
static char g_oled_cache[8][OLED_TEXT_COLS + 1U] = {{0}};
static uint8_t g_oled_cache_valid = 0U;
static uint8_t g_last_oled_page = 0xFFU;

/* Runtime flags describing refresh requests and IMU availability. */
static uint8_t g_ui_force_refresh = 1U;
static uint8_t g_mpu_basic_ready = 0U;
static uint8_t g_mpu_dmp_ready = 0U;

/* Last execution timestamps for the cooperative scheduler. */
static uint32_t g_last_key_ms = 0U;
static uint32_t g_last_mpu_ms = 0U;
static uint32_t g_last_adc_ms = 0U;
static uint32_t g_last_rtc_ms = 0U;
static uint32_t g_last_dht_ms = 0U;
static uint32_t g_last_max30102_ms = 0U;
static uint32_t g_last_oled_ms = 0U;
static uint32_t g_last_bt_ms = 0U;
static uint32_t g_boot_ms = 0U;

/* Step detector history used to adapt the threshold and avoid double counting. */
static int16_t g_step_prev_ax = 0;
static int16_t g_step_prev_ay = 0;
static int16_t g_step_prev_az = 0;
static uint32_t g_step_history[10] = {0};
static uint8_t g_step_history_index = 0U;
static uint16_t g_step_threshold = 700U;
static uint8_t g_step_gate = 0U;

/* Median-filter history and stability counters for pulse output. */
static uint16_t g_hr_history[HR_FILTER_DEPTH] = {0};
static uint8_t g_hr_history_count = 0U;
static uint8_t g_hr_history_index = 0U;

static uint8_t g_hr_consecutive_valid = 0U;
static uint8_t g_spo2_consecutive_valid = 0U;
static uint8_t g_hr_consecutive_invalid = 0U;
static uint8_t g_spo2_consecutive_invalid = 0U;
static uint8_t g_hr_jump_reject_count = 0U;

/* Non-blocking buzzer sequence state. */
static uint8_t g_alarm_on = 0U;
static uint8_t g_alarm_steps_left = 0U;
static uint32_t g_alarm_next_toggle_ms = 0U;
static uint32_t g_alarm_last_sequence_ms = 0U;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
static uint8_t App_LightPercentFromRaw(uint32_t raw);
static uint32_t App_AccelAbsSum(int16_t ax, int16_t ay, int16_t az);
static void App_StepAdjustThreshold(uint32_t current_sum);
static void App_StepUpdate(int16_t ax, int16_t ay, int16_t az);
static void App_BuzzerStart(uint16_t freq_hz);
static void App_BuzzerStop(void);
static void App_AlarmTask(uint32_t now, uint8_t alert_active);
static void App_RtcTask(void);
static void App_DhtTask(void);
static void App_LightTask(void);
static void App_MotionTask(void);
static void App_PulseTask(void);
static void App_KeysTask(void);
static void App_OledTask(void);
static void App_BluetoothTask(void);
static void App_GpsHandleByte(uint8_t byte);
static void App_BtHandleByte(uint8_t byte);
static void App_GpsProcessPending(void);
static void App_BtProcessPending(void);
static void App_GpsParseSentence(char *sentence);
static void App_GpsParseSentenceToState(gps_state_t *state, char *sentence);
static void App_BtParseSentence(char *sentence);
static uint8_t App_GpsStateFresh(const gps_state_t *state, uint32_t now);
static const gps_state_t *App_GpsGetActive(uint32_t now, uint8_t *from_bt);
static uint16_t App_FilterHeartRate(uint16_t sample);
static void App_UpdateTiltFromAccel(int16_t ax, int16_t ay, int16_t az);
static void App_RtcAddOffset(rtc_state_t *rtc, int32_t seconds_offset);
static const char *App_ClimateComfortText(uint8_t temp_c, uint8_t humidity_percent);
static const char *App_TemperatureComfortText(uint8_t temp_c);
static const char *App_HumidityComfortText(uint8_t humidity_percent);
static const char *App_LightLevelText(uint8_t percent);
static void App_OledWriteLine(uint8_t row, const char *text, uint8_t force);
static void App_NmeaToDegrees(const char *nmea, char hemisphere, uint8_t is_latitude, char *out, size_t out_len);
static uint8_t App_BtParseCoordinateText(const char *sentence, gps_state_t *state);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
/* Convert the raw 12-bit ADC sample into a user-facing percentage. */
static uint8_t App_LightPercentFromRaw(uint32_t raw)
{
    uint32_t percent = (raw * 100U) / 4095U;

#if !LIGHT_SENSOR_HIGH_WHEN_BRIGHT
    percent = 100U - percent;
#endif

    if (percent > 100U)
    {
        percent = 100U;
    }
    return (uint8_t)percent;
}

/* Compact motion magnitude used by the simple step detector. */
static uint32_t App_AccelAbsSum(int16_t ax, int16_t ay, int16_t az)
{
    return (uint32_t)abs((int)ax) + (uint32_t)abs((int)ay) + (uint32_t)abs((int)az);
}

/* Adapt the step threshold to recent motion intensity so the counter is less sensitive to fixed tuning. */
static void App_StepAdjustThreshold(uint32_t current_sum)
{
    uint32_t avg = 0U;
    uint8_t count = 0U;
    uint8_t i;

    g_step_history[g_step_history_index] = current_sum;
    g_step_history_index = (uint8_t)((g_step_history_index + 1U) % 10U);

    for (i = 0U; i < 10U; i++)
    {
        if (g_step_history[i] > 0U)
        {
            avg += g_step_history[i];
            count++;
        }
    }

    if (count == 0U)
    {
        return;
    }

    avg /= count;
    g_step_threshold = (uint16_t)(avg + 450U);
    if (g_step_threshold < 550U)
    {
        g_step_threshold = 550U;
    }
    if (g_step_threshold > 1500U)
    {
        g_step_threshold = 1500U;
    }
}

/* Detect a new step from the change in acceleration magnitude and gate it until motion settles again. */
static void App_StepUpdate(int16_t ax, int16_t ay, int16_t az)
{
    uint32_t current_sum = App_AccelAbsSum(ax, ay, az);
    uint32_t last_sum = App_AccelAbsSum(g_step_prev_ax, g_step_prev_ay, g_step_prev_az);
    int32_t accel_diff = (int32_t)current_sum - (int32_t)last_sum;

    if (accel_diff < 0)
    {
        accel_diff = -accel_diff;
    }

    App_StepAdjustThreshold(current_sum);

    if ((uint32_t)accel_diff > g_step_threshold)
    {
        if (g_step_gate == 0U)
        {
            g_motion.steps++;
            g_step_gate = 1U;
            g_ui_force_refresh = 1U;
        }
    }
    else if ((uint32_t)accel_diff < (g_step_threshold / 3U))
    {
        g_step_gate = 0U;
    }

    g_step_prev_ax = ax;
    g_step_prev_ay = ay;
    g_step_prev_az = az;
}

/* Fallback orientation estimate used when the MPU DMP is unavailable. */
static void App_UpdateTiltFromAccel(int16_t ax, int16_t ay, int16_t az)
{
    float axf = (float)ax;
    float ayf = (float)ay;
    float azf = (float)az;
    float denom = sqrtf((ayf * ayf) + (azf * azf));

    if (denom < 1.0f)
    {
        denom = 1.0f;
    }

    g_motion.pitch = atan2f(-axf, denom) * 57.2957795f;
    g_motion.roll = atan2f(ayf, azf) * 57.2957795f;
}

/* Calendar helpers are used when applying a display-side time offset. */
static uint8_t App_IsLeapYear(uint16_t year)
{
    uint16_t full_year = (uint16_t)(2000U + year);
    return (((full_year % 4U) == 0U) && (((full_year % 100U) != 0U) || ((full_year % 400U) == 0U))) ? 1U : 0U;
}

static uint8_t App_DaysInMonth(uint8_t month, uint8_t year)
{
    static const uint8_t days[12] = {31U, 28U, 31U, 30U, 31U, 30U, 31U, 31U, 30U, 31U, 30U, 31U};

    if ((month == 0U) || (month > 12U))
    {
        return 31U;
    }

    if ((month == 2U) && (App_IsLeapYear(year) != 0U))
    {
        return 29U;
    }

    return days[month - 1U];
}

/* Shift the displayed time without writing the offset back into the RTC chip. */
static void App_RtcAddOffset(rtc_state_t *rtc, int32_t seconds_offset)
{
    int32_t total_seconds;
    uint8_t dim;

    if ((rtc == NULL) || (rtc->valid == 0U) || (seconds_offset == 0))
    {
        return;
    }

    total_seconds = ((int32_t)rtc->hour * 3600) + ((int32_t)rtc->minute * 60) + (int32_t)rtc->second + seconds_offset;

    while (total_seconds < 0)
    {
        total_seconds += 86400;
        if (rtc->day > 1U)
        {
            rtc->day--;
        }
        else
        {
            if (rtc->month > 1U)
            {
                rtc->month--;
            }
            else
            {
                rtc->month = 12U;
                if (rtc->year > 0U)
                {
                    rtc->year--;
                }
            }
            rtc->day = App_DaysInMonth(rtc->month, rtc->year);
        }
    }

    while (total_seconds >= 86400)
    {
        total_seconds -= 86400;
        dim = App_DaysInMonth(rtc->month, rtc->year);
        rtc->day++;
        if (rtc->day > dim)
        {
            rtc->day = 1U;
            rtc->month++;
            if (rtc->month > 12U)
            {
                rtc->month = 1U;
                rtc->year++;
            }
        }
    }

    rtc->hour = (uint8_t)(total_seconds / 3600);
    total_seconds %= 3600;
    rtc->minute = (uint8_t)(total_seconds / 60);
    rtc->second = (uint8_t)(total_seconds % 60);
}

/* Human-readable labels used on the OLED and inside telemetry packets. */
static const char *App_TemperatureComfortText(uint8_t temp_c)
{
    if (temp_c < 20U)
    {
        return "Cold";
    }
    if (temp_c > 26U)
    {
        return "Hot";
    }
    return "Comfort";
}

static const char *App_HumidityComfortText(uint8_t humidity_percent)
{
    if (humidity_percent < 30U)
    {
        return "Dry";
    }
    if (humidity_percent > 60U)
    {
        return "Humid";
    }
    return "Comfort";
}

static const char *App_LightLevelText(uint8_t percent)
{
    if (percent >= 70U)
    {
        return "Bright";
    }
    if (percent >= 35U)
    {
        return "Normal";
    }
    return "Dark";
}

static const char *App_ClimateComfortText(uint8_t temp_c, uint8_t humidity_percent)
{
    uint8_t temp_ok = ((temp_c >= 20U) && (temp_c <= 26U)) ? 1U : 0U;
    uint8_t humidity_ok = ((humidity_percent >= 30U) && (humidity_percent <= 60U)) ? 1U : 0U;

    if ((temp_ok != 0U) && (humidity_ok != 0U))
    {
        return "Comfort";
    }
    if ((temp_c > 26U) && (humidity_percent > 60U))
    {
        return "Hot&Humid";
    }
    if ((temp_c < 20U) && (humidity_percent < 30U))
    {
        return "Cold&Dry";
    }
    if (temp_c > 26U)
    {
        return "Hot";
    }
    if (temp_c < 20U)
    {
        return "Cold";
    }
    if (humidity_percent > 60U)
    {
        return "Humid";
    }
    if (humidity_percent < 30U)
    {
        return "Dry";
    }
    return "NearComfort";
}

/* Pad, cache, and rewrite a single OLED row only when its visible content changes. */
static void App_OledWriteLine(uint8_t row, const char *text, uint8_t force)
{
    char padded[OLED_TEXT_COLS + 1U];
    size_t len;

    if (row >= 8U)
    {
        return;
    }

    memset(padded, ' ', sizeof(padded));
    padded[OLED_TEXT_COLS] = '\0';

    if (text != NULL)
    {
        len = strlen(text);
        if (len > OLED_TEXT_COLS)
        {
            len = OLED_TEXT_COLS;
        }
        memcpy(padded, text, len);
    }

    if ((force != 0U) || (g_oled_cache_valid == 0U) || (strncmp(g_oled_cache[row], padded, OLED_TEXT_COLS) != 0))
    {
        strcpy(g_oled_cache[row], padded);
        OLED_ShowString(0, row, g_oled_cache[row], 12, 0);
    }
}

/* Convert ddmm.mmmm / dddmm.mmmm NMEA text into a compact hemisphere-prefixed decimal string. */
static void App_NmeaToDegrees(const char *nmea, char hemisphere, uint8_t is_latitude, char *out, size_t out_len)
{
    uint32_t raw = 0U;
    uint32_t degrees;
    uint32_t minutes_x10000;
    uint32_t fraction_scale = 1U;
    const char *dot;
    size_t i;
    uint32_t decimal_deg_x10000;

    if ((out == NULL) || (out_len == 0U))
    {
        return;
    }

    out[0] = '\0';

    if ((nmea == NULL) || (nmea[0] == '\0'))
    {
        return;
    }

    for (i = 0U; nmea[i] != '\0'; i++)
    {
        if (nmea[i] == '.')
        {
            continue;
        }
        if ((nmea[i] < '0') || (nmea[i] > '9'))
        {
            return;
        }
        raw = (raw * 10U) + (uint32_t)(nmea[i] - '0');
    }

    dot = strchr(nmea, '.');
    if (dot != NULL)
    {
        size_t frac_digits = strlen(dot + 1U);
        while (frac_digits-- > 0U)
        {
            fraction_scale *= 10U;
        }
    }
    else
    {
        fraction_scale = 1U;
    }

    if (is_latitude != 0U)
    {
        degrees = raw / (100U * fraction_scale);
        minutes_x10000 = (raw - (degrees * 100U * fraction_scale)) * 10000U / fraction_scale;
    }
    else
    {
        degrees = raw / (100U * fraction_scale);
        minutes_x10000 = (raw - (degrees * 100U * fraction_scale)) * 10000U / fraction_scale;
    }

    decimal_deg_x10000 = (degrees * 10000U) + (minutes_x10000 / 60U);
    snprintf(out, out_len, "%c%lu.%04lu",
             hemisphere != '\0' ? hemisphere : '?',
             (unsigned long)(decimal_deg_x10000 / 10000U),
             (unsigned long)(decimal_deg_x10000 % 10000U));
}

/* Treat old fixes as stale so the UI can fall back to Bluetooth-relayed coordinates when necessary. */
static uint8_t App_GpsStateFresh(const gps_state_t *state, uint32_t now)
{
    if ((state == NULL) || (state->fix_valid == 0U) || (state->last_update_ms == 0U))
    {
        return 0U;
    }

    return ((now - state->last_update_ms) <= GPS_STALE_MS) ? 1U : 0U;
}

/* Prefer the dedicated GNSS receiver, but transparently fall back to a fresh Bluetooth position source. */
static const gps_state_t *App_GpsGetActive(uint32_t now, uint8_t *from_bt)
{
    if (App_GpsStateFresh(&g_gps, now) != 0U)
    {
        if (from_bt != NULL)
        {
            *from_bt = 0U;
        }
        return &g_gps;
    }

    if (App_GpsStateFresh(&g_bt_gps, now) != 0U)
    {
        if (from_bt != NULL)
        {
            *from_bt = 1U;
        }
        return &g_bt_gps;
    }

    if (from_bt != NULL)
    {
        *from_bt = 0U;
    }
    return NULL;
}

/* Small median filter to suppress short heart-rate spikes before smoothing is applied. */
static uint16_t App_FilterHeartRate(uint16_t sample)
{
    uint16_t temp[HR_FILTER_DEPTH];
    uint8_t count;
    uint8_t i;
    uint8_t j;
    uint16_t key;

    g_hr_history[g_hr_history_index] = sample;
    g_hr_history_index = (uint8_t)((g_hr_history_index + 1U) % HR_FILTER_DEPTH);
    if (g_hr_history_count < HR_FILTER_DEPTH)
    {
        g_hr_history_count++;
    }

    count = g_hr_history_count;
    for (i = 0U; i < count; i++)
    {
        temp[i] = g_hr_history[i];
    }

    for (i = 1U; i < count; i++)
    {
        key = temp[i];
        j = i;
        while ((j > 0U) && (temp[j - 1U] > key))
        {
            temp[j] = temp[j - 1U];
            j--;
        }
        temp[j] = key;
    }

    return temp[count / 2U];
}

/* Drive TIM3 PWM so the buzzer can be sequenced without blocking the main loop. */
static void App_BuzzerStart(uint16_t freq_hz)
{
    uint32_t arr;
    uint32_t pulse;

    if (freq_hz == 0U)
    {
        App_BuzzerStop();
        return;
    }

    arr = 1000000UL / (uint32_t)freq_hz;
    if (arr > 0U)
    {
        arr -= 1U;
    }
    if (arr > 0xFFFFU)
    {
        arr = 0xFFFFU;
    }

    pulse = (arr + 1U) / 2U;
    __HAL_TIM_SET_AUTORELOAD(&htim3, (uint16_t)arr);
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_3, (uint16_t)pulse);
    HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_3);
    g_alarm_on = 1U;
}

static void App_BuzzerStop(void)
{
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_3, 0U);
    HAL_TIM_PWM_Stop(&htim3, TIM_CHANNEL_3);
    g_alarm_on = 0U;
}

/* Generate a repeating beep pattern while either temperature or heart-rate alarms are active. */
static void App_AlarmTask(uint32_t now, uint8_t alert_active)
{
    if (alert_active != 0U)
    {
        if ((g_alarm_steps_left == 0U) && ((now - g_alarm_last_sequence_ms) >= 1500U))
        {
            g_alarm_steps_left = 6U;
            g_alarm_last_sequence_ms = now;
            App_BuzzerStart(2000U);
            g_alarm_next_toggle_ms = now + 100U;
        }
    }
    else
    {
        g_alarm_steps_left = 0U;
        App_BuzzerStop();
        return;
    }

    if ((g_alarm_steps_left > 0U) && (now >= g_alarm_next_toggle_ms))
    {
        if (g_alarm_on != 0U)
        {
            App_BuzzerStop();
            g_alarm_next_toggle_ms = now + 80U;
        }
        else
        {
            App_BuzzerStart(2000U);
            g_alarm_next_toggle_ms = now + 100U;
        }

        g_alarm_steps_left--;
        if (g_alarm_steps_left == 0U)
        {
            App_BuzzerStop();
        }
    }
}

/* Refresh the RTC snapshot used by both the OLED and telemetry packet. */
static void App_RtcTask(void)
{
    uint8_t *time_ptr;

    DS3231_ReadTime();
    time_ptr = DS3231_ReadTime_ReturnPoint();
    g_rtc.year = time_ptr[1];
    g_rtc.month = time_ptr[2];
    g_rtc.day = time_ptr[3];
    g_rtc.weekday = time_ptr[4];
    g_rtc.hour = time_ptr[5];
    g_rtc.minute = time_ptr[6];
    g_rtc.second = time_ptr[7];
    g_rtc.valid = 1U;
    App_RtcAddOffset(&g_rtc, RTC_DISPLAY_OFFSET_SECONDS);
}

/* Read the DHT11 and mark the module invalid when the checksum/handshake fails. */
static void App_DhtTask(void)
{
    if (DH11_Read() != 0U)
    {
        g_dht.temp = DH11_data.temp;
        g_dht.humidity = DH11_data.humidity;
        g_dht.valid = 1U;
    }
    else
    {
        g_dht.valid = 0U;
    }
}

/* Sample the light sensor through the ADC and convert it to a percentage. */
static void App_LightTask(void)
{
    if (HAL_ADC_Start(&hadc1) == HAL_OK)
    {
        if (HAL_ADC_PollForConversion(&hadc1, 5U) == HAL_OK)
        {
            g_light.raw = HAL_ADC_GetValue(&hadc1);
            g_light.percent = App_LightPercentFromRaw(g_light.raw);
        }
        HAL_ADC_Stop(&hadc1);
    }
}

/* Update raw IMU data, step count, and orientation estimates. */
static void App_MotionTask(void)
{
    uint8_t accel_ok = 0U;

    if (g_mpu_basic_ready == 0U)
    {
        g_motion.valid = 0U;
        return;
    }

    if (MPU_Get_Accelerometer(&g_motion.ax, &g_motion.ay, &g_motion.az) == 0U)
    {
        accel_ok = 1U;
        g_motion.valid = 1U;
        App_StepUpdate(g_motion.ax, g_motion.ay, g_motion.az);
    }

    (void)MPU_Get_Gyroscope(&g_motion.gx, &g_motion.gy, &g_motion.gz);

    if ((g_mpu_dmp_ready != 0U) && (mpu_dmp_get_data(&g_motion.pitch, &g_motion.roll, &g_motion.yaw) == 0U))
    {
        g_motion.valid = 1U;
    }
    else if (accel_ok != 0U)
    {
        App_UpdateTiltFromAccel(g_motion.ax, g_motion.ay, g_motion.az);
        g_motion.yaw = 0.0f;
        g_motion.valid = 1U;
    }
    else
    {
        g_motion.valid = 0U;
    }
}

/* Update MAX30102 processing: require stable windows, reset when the finger is removed, and smooth valid outputs. */
static void App_PulseTask(void)
{
    uint8_t result_updated;
    uint8_t finger_before = g_pulse.finger_present;
    uint16_t hr_candidate = 0U;
    uint16_t spo2_candidate = 0U;
    uint16_t diff;

    Int_MAX30102_GetSpo2AndHeartRate();
    g_pulse.finger_present = Int_MAX30102_FingerDetected();
    result_updated = Int_MAX30102_ResultUpdated();

    if (g_pulse.finger_present != finger_before)
    {
        g_ui_force_refresh = 1U;
    }

    if (g_pulse.finger_present == 0U)
    {
        g_pulse.heart_rate = 0U;
        g_pulse.spo2 = 0U;
        g_pulse.hr_valid = 0U;
        g_pulse.spo2_valid = 0U;
        g_hr_consecutive_valid = 0U;
        g_spo2_consecutive_valid = 0U;
        g_hr_consecutive_invalid = 0U;
        g_spo2_consecutive_invalid = 0U;
        g_hr_jump_reject_count = 0U;
        memset(g_hr_history, 0, sizeof(g_hr_history));
        g_hr_history_count = 0U;
        g_hr_history_index = 0U;
        return;
    }

    if (result_updated == 0U)
    {
        return;
    }

    if ((ch_hr_valid != 0U) && (n_heart_rate >= 45) && (n_heart_rate <= 165))
    {
        hr_candidate = App_FilterHeartRate((uint16_t)n_heart_rate);

        if (g_hr_consecutive_valid == 0U)
        {
            g_pulse.heart_rate = hr_candidate;
        }
        else
        {
            diff = (g_pulse.heart_rate > hr_candidate)
                 ? (uint16_t)(g_pulse.heart_rate - hr_candidate)
                 : (uint16_t)(hr_candidate - g_pulse.heart_rate);

            if ((g_hr_consecutive_valid >= 2U) && (diff > 15U) && (g_hr_jump_reject_count < 2U))
            {
                hr_candidate = g_pulse.heart_rate;
                g_hr_jump_reject_count++;
            }
            else
            {
                g_hr_jump_reject_count = 0U;
            }

            g_pulse.heart_rate = (uint16_t)(((uint32_t)g_pulse.heart_rate * 7U + (uint32_t)hr_candidate + 4U) / 8U);
        }

        if (g_hr_consecutive_valid < 4U)
        {
            g_hr_consecutive_valid++;
        }
        g_hr_consecutive_invalid = 0U;
        g_pulse.hr_valid = (g_hr_consecutive_valid >= 2U) ? 1U : 0U;
    }
    else
    {
        if (g_hr_consecutive_invalid < 4U)
        {
            g_hr_consecutive_invalid++;
        }
        if (g_hr_consecutive_invalid >= MAX30102_INVALID_HOLD)
        {
            g_pulse.heart_rate = 0U;
            g_pulse.hr_valid = 0U;
            g_hr_consecutive_valid = 0U;
            g_hr_jump_reject_count = 0U;
            memset(g_hr_history, 0, sizeof(g_hr_history));
            g_hr_history_count = 0U;
            g_hr_history_index = 0U;
        }
    }

    if ((ch_spo2_valid != 0U) && (n_sp02 >= 80) && (n_sp02 <= 100))
    {
        spo2_candidate = (uint16_t)n_sp02;
        if (g_spo2_consecutive_valid == 0U)
        {
            g_pulse.spo2 = spo2_candidate;
        }
        else
        {
            g_pulse.spo2 = (uint16_t)(((uint32_t)g_pulse.spo2 * 7U + (uint32_t)spo2_candidate + 4U) / 8U);
        }

        if (g_spo2_consecutive_valid < 4U)
        {
            g_spo2_consecutive_valid++;
        }
        g_spo2_consecutive_invalid = 0U;
        g_pulse.spo2_valid = (g_spo2_consecutive_valid >= 2U) ? 1U : 0U;
    }
    else
    {
        if (g_spo2_consecutive_invalid < 4U)
        {
            g_spo2_consecutive_invalid++;
        }
        if (g_spo2_consecutive_invalid >= MAX30102_INVALID_HOLD)
        {
            g_pulse.spo2 = 0U;
            g_pulse.spo2_valid = 0U;
            g_spo2_consecutive_valid = 0U;
        }
    }

    g_ui_force_refresh = 1U;
}

/* Page navigation and threshold adjustment are handled by four active-low buttons. */
static void App_KeysTask(void)
{
    static uint8_t last_pressed[4] = {0U, 0U, 0U, 0U};
    uint8_t current_pressed[4];

    /* Read key states, active low. */
    current_pressed[0] = (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_4) == GPIO_PIN_RESET) ? 1U : 0U; /* PA4 */
    current_pressed[1] = (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_5) == GPIO_PIN_RESET) ? 1U : 0U; /* PA5 */
    current_pressed[2] = (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_6) == GPIO_PIN_RESET) ? 1U : 0U; /* PA6 */
    current_pressed[3] = (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_7) == GPIO_PIN_RESET) ? 1U : 0U; /* PA7 */

    /* PA4: previous page */
    if ((current_pressed[0] != 0U) && (last_pressed[0] == 0U))
    {
        if (g_ui.page == 0U)
        {
            g_ui.page = (uint8_t)(MENU_PAGE_COUNT - 1U);
        }
        else
        {
            g_ui.page--;
        }
        g_ui_force_refresh = 1U;
    }

    /* PA5: next page */
    if ((current_pressed[1] != 0U) && (last_pressed[1] == 0U))
    {
        g_ui.page = (uint8_t)((g_ui.page + 1U) % MENU_PAGE_COUNT);
        g_ui_force_refresh = 1U;
    }

    /* PA6: increase threshold or reset steps on Motion page */
    if ((current_pressed[2] != 0U) && (last_pressed[2] == 0U))
    {
        if ((g_ui.page == 1U) && (g_ui.temp_warn < TEMP_WARN_MAX))
        {
            g_ui.temp_warn++;
            g_ui_force_refresh = 1U;
        }
        else if ((g_ui.page == 2U) && (g_ui.hr_warn < HR_WARN_MAX))
        {
            g_ui.hr_warn++;
            g_ui_force_refresh = 1U;
        }
        else if (g_ui.page == 5U)
        {
            g_motion.steps = 0U;
            memset(g_step_history, 0, sizeof(g_step_history));
            g_step_threshold = 700U;
            g_step_gate = 0U;
            g_ui_force_refresh = 1U;
        }
    }

    /* PA7: decrease threshold */
    if ((current_pressed[3] != 0U) && (last_pressed[3] == 0U))
    {
        if ((g_ui.page == 1U) && (g_ui.temp_warn > TEMP_WARN_MIN))
        {
            g_ui.temp_warn--;
            g_ui_force_refresh = 1U;
        }
        else if ((g_ui.page == 2U) && (g_ui.hr_warn > HR_WARN_MIN))
        {
            g_ui.hr_warn--;
            g_ui_force_refresh = 1U;
        }
    }

    memcpy(last_pressed, current_pressed, sizeof(last_pressed));
}

/* Render one page at a time and only rewrite lines that changed to reduce flicker. */
static void App_OledTask(void)
{
    char rows[8][OLED_TEXT_COLS + 1U];
    char line[OLED_TEXT_COLS + 1U];
    const gps_state_t *active_gps;
    uint8_t gps_from_bt = 0U;
    uint32_t now = HAL_GetTick();
    uint8_t row;
    uint8_t force = 0U;

    for (row = 0U; row < 8U; row++)
    {
        rows[row][0] = '\0';
    }

    if ((g_oled_cache_valid == 0U) || (g_last_oled_page != g_ui.page))
    {
        force = 1U;
        g_last_oled_page = g_ui.page;
    }

    if (g_ui.page == 0U)
    {
        strcpy(rows[0], "RTC");
        if (g_rtc.valid != 0U)
        {
            snprintf(rows[2], sizeof(rows[2]), "20%02u-%02u-%02u", g_rtc.year, g_rtc.month, g_rtc.day);
            snprintf(rows[4], sizeof(rows[4]), "%02u:%02u:%02u", g_rtc.hour, g_rtc.minute, g_rtc.second);
        }
        else
        {
            strcpy(rows[2], "RTC NOT READY");
        }
    }
    else if (g_ui.page == 1U)
    {
        strcpy(rows[0], "Temp / Humi");
        if (g_dht.valid != 0U)
        {
            snprintf(rows[2], sizeof(rows[2]), "Temp:%2uC", g_dht.temp);
            snprintf(rows[3], sizeof(rows[3]), "Temp:%s", App_TemperatureComfortText(g_dht.temp));
            snprintf(rows[5], sizeof(rows[5]), "Humi:%2u%%", g_dht.humidity);
            snprintf(rows[6], sizeof(rows[6]), "Hum :%s", App_HumidityComfortText(g_dht.humidity));
        }
        else
        {
            strcpy(rows[2], "Temp:--C");
            strcpy(rows[3], "Temp:--");
            strcpy(rows[5], "Humi:--%");
            strcpy(rows[6], "Hum :--");
        }
        snprintf(rows[7], sizeof(rows[7]), "Warn:%2uC", g_ui.temp_warn);
    }
    else if (g_ui.page == 2U)
    {
        strcpy(rows[0], "Heart Rate & SPO2");
        if (g_pulse.spo2_valid != 0U)
        {
            snprintf(rows[2], sizeof(rows[2]), "SPO2:%3u%%", g_pulse.spo2);
        }
        else
        {
            strcpy(rows[2], "SPO2:---%");
        }

        if (g_pulse.hr_valid != 0U)
        {
            snprintf(rows[4], sizeof(rows[4]), "HR:%3ubpm", g_pulse.heart_rate);
        }
        else
        {
            strcpy(rows[4], "HR:---bpm");
        }

        snprintf(rows[6], sizeof(rows[6]), "Warn:%3u", g_ui.hr_warn);
        strcpy(rows[7], (g_pulse.finger_present != 0U) ? "Finger:On" : "Finger:Off");
    }
    else if (g_ui.page == 3U)
    {
        active_gps = App_GpsGetActive(now, &gps_from_bt);
        strcpy(rows[0], "GPS / BT");

        if (active_gps != NULL)
        {
            snprintf(rows[2], sizeof(rows[2]), "SRC:%s FIX", (gps_from_bt != 0U) ? "BT" : "GNSS");
            snprintf(rows[4], sizeof(rows[4]), "Lat:%s", active_gps->lat_deg[0] != '\0' ? active_gps->lat_deg : active_gps->lat);
            snprintf(rows[6], sizeof(rows[6]), "Lon:%s", active_gps->lon_deg[0] != '\0' ? active_gps->lon_deg : active_gps->lon);
        }
        else
        {
            strcpy(rows[2], "SRC:None");
            strcpy(rows[4], "Lat:--");
            strcpy(rows[6], "Lon:--");
            strcpy(rows[7], "Send NMEA over BT");
        }
    }
    else if (g_ui.page == 4U)
    {
        strcpy(rows[0], "Light Sensor");
        snprintf(rows[2], sizeof(rows[2]), "ADC:%4lu", (unsigned long)g_light.raw);
        snprintf(rows[4], sizeof(rows[4]), "Light:%3u%%", g_light.percent);
        snprintf(rows[6], sizeof(rows[6]), "Level:%s", App_LightLevelText(g_light.percent));
    }
    else
    {
        strcpy(rows[0], "Motion");
        snprintf(rows[2], sizeof(rows[2]), "Step:%05lu", (unsigned long)g_motion.steps);

        if (g_motion.valid != 0U)
        {
            snprintf(rows[4], sizeof(rows[4]), "Roll:%4d", (int)g_motion.roll);
            snprintf(rows[5], sizeof(rows[5]), "Pitch:%4d", (int)g_motion.pitch);
            snprintf(rows[6], sizeof(rows[6]), "Yaw :%4d", (int)g_motion.yaw);
        }
        else
        {
            strcpy(rows[4], "MPU CHECK WIRE");
            strcpy(rows[6], "No motion data");
        }
    }

    for (row = 0U; row < 8U; row++)
    {
        strncpy(line, rows[row], sizeof(line) - 1U);
        line[sizeof(line) - 1U] = '\0';
        App_OledWriteLine(row, line, force);
    }
    g_oled_cache_valid = 1U;
}

/* Emit one consolidated telemetry sentence for the desktop dashboard to parse. */
static void App_BluetoothTask(void)
{
    const char *comfort = (g_dht.valid != 0U) ? App_ClimateComfortText(g_dht.temp, g_dht.humidity) : "--";
    const char *hr_text = "--";
    const char *spo2_text = "--";
    const gps_state_t *active_gps;
    char hr_buf[8];
    char spo2_buf[8];
    uint8_t gps_from_bt = 0U;
    const char *src = "NONE";
    char posfix = 'V';
    uint32_t now = HAL_GetTick();

    active_gps = App_GpsGetActive(now, &gps_from_bt);
    if (active_gps != NULL)
    {
        src = (gps_from_bt != 0U) ? "BT" : "GNSS";
        posfix = 'A';
    }

    if ((g_pulse.hr_valid != 0U) && (g_pulse.finger_present != 0U))
    {
        snprintf(hr_buf, sizeof(hr_buf), "%u", g_pulse.heart_rate);
        hr_text = hr_buf;
    }
    if ((g_pulse.spo2_valid != 0U) && (g_pulse.finger_present != 0U))
    {
        snprintf(spo2_buf, sizeof(spo2_buf), "%u", g_pulse.spo2);
        spo2_text = spo2_buf;
    }

   printf("TIME=20%02u-%02u-%02u %02u:%02u:%02u,"
       "TEMP=%u,HUMI=%u,COMFORT=%s,"
       "HR=%s,SPO2=%s,FINGER=%s,"
       "STEP=%lu,LIGHT=%u,ADC_RAW=%lu,"
       "ROLL=%d,PITCH=%d,YAW=%d,"
       "AX=%d,AY=%d,AZ=%d,GX=%d,GY=%d,GZ=%d,"
       "TEMP_WARN=%u,HR_WARN=%u,"
       "POSSRC=%s,POSFIX=%c,%s,%s\r\n",
       g_rtc.year, g_rtc.month, g_rtc.day,
       g_rtc.hour, g_rtc.minute, g_rtc.second,
       g_dht.temp, g_dht.humidity, comfort,
       hr_text, spo2_text, (g_pulse.finger_present != 0U) ? "ON" : "OFF",
       (unsigned long)g_motion.steps,
       g_light.percent,
       (unsigned long)g_light.raw,
       (int)g_motion.roll,
       (int)g_motion.pitch,
       (int)g_motion.yaw,
       (int)g_motion.ax,
       (int)g_motion.ay,
       (int)g_motion.az,
       (int)g_motion.gx,
       (int)g_motion.gy,
       (int)g_motion.gz,
       g_ui.temp_warn,
       g_ui.hr_warn,
       src,
       posfix,
       (active_gps != NULL) ? (active_gps->lat_deg[0] != '\0' ? active_gps->lat_deg : active_gps->lat) : "--",
       (active_gps != NULL) ? (active_gps->lon_deg[0] != '\0' ? active_gps->lon_deg : active_gps->lon) : "--");

}

/* Assemble one GNSS line at a time from byte-wise UART interrupts. */
static void App_GpsHandleByte(uint8_t byte)
{
    if (byte == '\r')
    {
        return;
    }

    if (byte == '\n')
    {
        if ((g_gps_line_index > 0U) && (g_gps_sentence_ready == 0U))
        {
            g_gps_line_buffer[g_gps_line_index] = '\0';
            strncpy(g_gps_sentence_buffer, g_gps_line_buffer, GPS_LINE_MAX - 1U);
            g_gps_sentence_buffer[GPS_LINE_MAX - 1U] = '\0';
            g_gps_sentence_ready = 1U;
        }
        g_gps_line_index = 0U;
        g_gps_line_buffer[0] = '\0';
        return;
    }

    if (g_gps_line_index < (GPS_LINE_MAX - 1U))
    {
        g_gps_line_buffer[g_gps_line_index++] = (char)byte;
    }
    else
    {
        g_gps_line_index = 0U;
        g_gps_line_buffer[0] = '\0';
    }
}

/* Assemble Bluetooth-relayed location sentences using the same line-oriented logic as GNSS input. */
static void App_BtHandleByte(uint8_t byte)
{
    if (byte == '\r')
    {
        return;
    }

    if (byte == '\n')
    {
        if ((g_bt_line_index > 0U) && (g_bt_sentence_ready == 0U))
        {
            g_bt_line_buffer[g_bt_line_index] = '\0';
            strncpy(g_bt_sentence_buffer, g_bt_line_buffer, GPS_LINE_MAX - 1U);
            g_bt_sentence_buffer[GPS_LINE_MAX - 1U] = '\0';
            g_bt_sentence_ready = 1U;
        }
        g_bt_line_index = 0U;
        g_bt_line_buffer[0] = '\0';
        return;
    }

    if (g_bt_line_index < (GPS_LINE_MAX - 1U))
    {
        g_bt_line_buffer[g_bt_line_index++] = (char)byte;
    }
    else
    {
        g_bt_line_index = 0U;
        g_bt_line_buffer[0] = '\0';
    }
}

/* Parse a freshly completed GNSS sentence into the primary GPS cache. */
static void App_GpsParseSentence(char *sentence)
{
    App_GpsParseSentenceToState(&g_gps, sentence);
}

/*
 * Accept common NMEA sentences from the hardware GNSS or from a PC relayed over
 * Bluetooth. RMC provides date/time and position, GGA provides position/fix
 * quality, and GLL provides position with an A/V validity flag.
 */
static void App_GpsParseSentenceToState(gps_state_t *state, char *sentence)
{
    char local[GPS_LINE_MAX];
    char *token;
    uint8_t field = 0U;
    char type[8] = {0};
    char status = 'V';
    char lat[16] = {0};
    char lon[16] = {0};
    char utc[16] = {0};
    char date[8] = {0};
    char lat_hemi = '\0';
    char lon_hemi = '\0';
    uint8_t fix_valid = 0U;

    if ((state == NULL) || (sentence == NULL))
    {
        return;
    }

    while ((*sentence == ' ') || (*sentence == '	'))
    {
        sentence++;
    }

    if (sentence[0] != '$')
    {
        return;
    }

    strncpy(local, sentence, sizeof(local) - 1U);
    local[sizeof(local) - 1U] = '\0';

    token = strtok(local, ",");
    while (token != NULL)
    {
        switch (field)
        {
            case 0:
                strncpy(type, token, sizeof(type) - 1U);
                type[sizeof(type) - 1U] = '\0';
                break;

            /* RMC: $GxRMC,time,status,lat,N,lon,E,speed,course,date,... */
            case 1:
                if ((strncmp(type, "$GPRMC", 6U) == 0) || (strncmp(type, "$GNRMC", 6U) == 0)
                 || (strncmp(type, "$GPGGA", 6U) == 0) || (strncmp(type, "$GNGGA", 6U) == 0))
                {
                    strncpy(utc, token, sizeof(utc) - 1U);
                    utc[sizeof(utc) - 1U] = '\0';
                }
                else if ((strncmp(type, "$GPGLL", 6U) == 0) || (strncmp(type, "$GNGLL", 6U) == 0))
                {
                    strncpy(lat, token, sizeof(lat) - 1U);
                    lat[sizeof(lat) - 1U] = '\0';
                }
                break;

            case 2:
                if ((strncmp(type, "$GPRMC", 6U) == 0) || (strncmp(type, "$GNRMC", 6U) == 0))
                {
                    if (token[0] != '\0')
                    {
                        status = token[0];
                    }
                }
                else if ((strncmp(type, "$GPGGA", 6U) == 0) || (strncmp(type, "$GNGGA", 6U) == 0))
                {
                    strncpy(lat, token, sizeof(lat) - 1U);
                    lat[sizeof(lat) - 1U] = '\0';
                }
                else if ((strncmp(type, "$GPGLL", 6U) == 0) || (strncmp(type, "$GNGLL", 6U) == 0))
                {
                    if (token[0] != '\0')
                    {
                        lat_hemi = token[0];
                    }
                }
                break;

            case 3:
                if ((strncmp(type, "$GPRMC", 6U) == 0) || (strncmp(type, "$GNRMC", 6U) == 0))
                {
                    strncpy(lat, token, sizeof(lat) - 1U);
                    lat[sizeof(lat) - 1U] = '\0';
                }
                else if ((strncmp(type, "$GPGGA", 6U) == 0) || (strncmp(type, "$GNGGA", 6U) == 0))
                {
                    if (token[0] != '\0')
                    {
                        lat_hemi = token[0];
                    }
                }
                else if ((strncmp(type, "$GPGLL", 6U) == 0) || (strncmp(type, "$GNGLL", 6U) == 0))
                {
                    strncpy(lon, token, sizeof(lon) - 1U);
                    lon[sizeof(lon) - 1U] = '\0';
                }
                break;

            case 4:
                if ((strncmp(type, "$GPRMC", 6U) == 0) || (strncmp(type, "$GNRMC", 6U) == 0))
                {
                    if (token[0] != '\0')
                    {
                        lat_hemi = token[0];
                    }
                }
                else if ((strncmp(type, "$GPGGA", 6U) == 0) || (strncmp(type, "$GNGGA", 6U) == 0))
                {
                    strncpy(lon, token, sizeof(lon) - 1U);
                    lon[sizeof(lon) - 1U] = '\0';
                }
                else if ((strncmp(type, "$GPGLL", 6U) == 0) || (strncmp(type, "$GNGLL", 6U) == 0))
                {
                    if (token[0] != '\0')
                    {
                        lon_hemi = token[0];
                    }
                }
                break;

            case 5:
                if ((strncmp(type, "$GPRMC", 6U) == 0) || (strncmp(type, "$GNRMC", 6U) == 0))
                {
                    strncpy(lon, token, sizeof(lon) - 1U);
                    lon[sizeof(lon) - 1U] = '\0';
                }
                else if ((strncmp(type, "$GPGGA", 6U) == 0) || (strncmp(type, "$GNGGA", 6U) == 0))
                {
                    if (token[0] != '\0')
                    {
                        lon_hemi = token[0];
                    }
                }
                else if ((strncmp(type, "$GPGLL", 6U) == 0) || (strncmp(type, "$GNGLL", 6U) == 0))
                {
                    strncpy(utc, token, sizeof(utc) - 1U);
                    utc[sizeof(utc) - 1U] = '\0';
                }
                break;

            case 6:
                if ((strncmp(type, "$GPRMC", 6U) == 0) || (strncmp(type, "$GNRMC", 6U) == 0))
                {
                    if (token[0] != '\0')
                    {
                        lon_hemi = token[0];
                    }
                }
                else if ((strncmp(type, "$GPGGA", 6U) == 0) || (strncmp(type, "$GNGGA", 6U) == 0))
                {
                    if (token[0] > '0')
                    {
                        fix_valid = 1U;
                    }
                }
                else if ((strncmp(type, "$GPGLL", 6U) == 0) || (strncmp(type, "$GNGLL", 6U) == 0))
                {
                    if (token[0] != '\0')
                    {
                        status = token[0];
                    }
                }
                break;

            case 9:
                if ((strncmp(type, "$GPRMC", 6U) == 0) || (strncmp(type, "$GNRMC", 6U) == 0))
                {
                    strncpy(date, token, sizeof(date) - 1U);
                    date[sizeof(date) - 1U] = '\0';
                }
                break;

            default:
                break;
        }
        token = strtok(NULL, ",");
        field++;
    }

    if (utc[0] != '\0')
    {
        strncpy(state->utc, utc, sizeof(state->utc) - 1U);
        state->utc[sizeof(state->utc) - 1U] = '\0';
    }
    if (date[0] != '\0')
    {
        strncpy(state->date, date, sizeof(state->date) - 1U);
        state->date[sizeof(state->date) - 1U] = '\0';
    }

    if (((strncmp(type, "$GPRMC", 6U) == 0) || (strncmp(type, "$GNRMC", 6U) == 0)
      || (strncmp(type, "$GPGLL", 6U) == 0) || (strncmp(type, "$GNGLL", 6U) == 0))
     && (status == 'A'))
    {
        fix_valid = 1U;
    }

    if ((fix_valid != 0U) && (lat[0] != '\0') && (lon[0] != '\0'))
    {
        strncpy(state->lat, lat, sizeof(state->lat) - 1U);
        state->lat[sizeof(state->lat) - 1U] = '\0';
        strncpy(state->lon, lon, sizeof(state->lon) - 1U);
        state->lon[sizeof(state->lon) - 1U] = '\0';
        state->lat_hemi = lat_hemi;
        state->lon_hemi = lon_hemi;
        App_NmeaToDegrees(state->lat, state->lat_hemi, 1U, state->lat_deg, sizeof(state->lat_deg));
        App_NmeaToDegrees(state->lon, state->lon_hemi, 0U, state->lon_deg, sizeof(state->lon_deg));
        state->fix_valid = 1U;
        state->last_update_ms = HAL_GetTick();
    }
    else if (((strncmp(type, "$GPRMC", 6U) == 0) || (strncmp(type, "$GNRMC", 6U) == 0)
           || (strncmp(type, "$GPGGA", 6U) == 0) || (strncmp(type, "$GNGGA", 6U) == 0)
           || (strncmp(type, "$GPGLL", 6U) == 0) || (strncmp(type, "$GNGLL", 6U) == 0)))
    {
        state->fix_valid = 0U;
    }
}

//Accept raw NMEA from a PC, legacy BTGPS/PHONEGPS messages, or plain decimal
/* Accept decimal coordinates from legacy phone apps as well as BTGPS/PHONEGPS helper prefixes. */
static uint8_t App_BtParseCoordinateText(const char *sentence, gps_state_t *state)
{
    char local[GPS_LINE_MAX];
    char *first;
    char *second;
    char *end_ptr;
    double lat_value;
    double lon_value;

    if ((sentence == NULL) || (state == NULL))
    {
        return 0U;
    }

    while ((*sentence == ' ') || (*sentence == '	'))
    {
        sentence++;
    }

    if (strncmp(sentence, "BTGPS,", 6U) == 0)
    {
        sentence += 6U;
    }
    else if (strncmp(sentence, "PHONEGPS,", 9U) == 0)
    {
        sentence += 9U;
    }
    else if (strncmp(sentence, "geo:", 4U) == 0)
    {
        sentence += 4U;
    }
    else if (strncmp(sentence, "LAT=", 4U) == 0)
    {
        sentence += 4U;
    }

    strncpy(local, sentence, sizeof(local) - 1U);
    local[sizeof(local) - 1U] = '\0';

    if (strncmp(local, "LAT=", 4U) == 0)
    {
        memmove(local, local + 4, strlen(local + 4) + 1U);
    }
    if (strncmp(local, "LON=", 4U) == 0)
    {
        memmove(local, local + 4, strlen(local + 4) + 1U);
    }

    first = strtok(local, ",");
    second = strtok(NULL, ",");
    if ((first == NULL) || (second == NULL))
    {
        return 0U;
    }

    if (strncmp(second, "LON=", 4U) == 0)
    {
        second += 4U;
    }

    lat_value = strtod(first, &end_ptr);
    if ((end_ptr == first) || ((*end_ptr != '\0') && (*end_ptr != ' ')))
    {
        return 0U;
    }
    lon_value = strtod(second, &end_ptr);
    if ((end_ptr == second) || ((*end_ptr != '\0') && (*end_ptr != ' ')))
    {
        return 0U;
    }

    if ((lat_value < -90.0) || (lat_value > 90.0) || (lon_value < -180.0) || (lon_value > 180.0))
    {
        return 0U;
    }

    memset(state, 0, sizeof(*state));
    snprintf(state->lat_deg, sizeof(state->lat_deg), "%.5f", lat_value);
    snprintf(state->lon_deg, sizeof(state->lon_deg), "%.5f", lon_value);
    state->fix_valid = 1U;
    state->last_update_ms = HAL_GetTick();
    return 1U;
}

/* Bluetooth may deliver full NMEA, simplified text coordinates, or an explicit "fix lost" command. */
static void App_BtParseSentence(char *sentence)
{
    if (sentence == NULL)
    {
        return;
    }

    while ((*sentence == ' ') || (*sentence == '\t'))
    {
        sentence++;
    }

    if (sentence[0] == '$')
    {
        App_GpsParseSentenceToState(&g_bt_gps, sentence);
        return;
    }

    if ((strcmp(sentence, "BTGPS,OFF") == 0) || (strcmp(sentence, "BTFIX,0") == 0))
    {
        memset(&g_bt_gps, 0, sizeof(g_bt_gps));
        return;
    }

    (void)App_BtParseCoordinateText(sentence, &g_bt_gps);
}

/* Promote a completed GNSS line from the interrupt buffer into the parsed GPS state. */
static void App_GpsProcessPending(void)
{
    if (g_gps_sentence_ready != 0U)
    {
        g_gps_sentence_ready = 0U;
        App_GpsParseSentence(g_gps_sentence_buffer);
        g_ui_force_refresh = 1U;
    }
}

/* Promote a completed Bluetooth line from the interrupt buffer into the fallback GPS state. */
static void App_BtProcessPending(void)
{
    if (g_bt_sentence_ready != 0U)
    {
        g_bt_sentence_ready = 0U;
        App_BtParseSentence(g_bt_sentence_buffer);
        g_ui_force_refresh = 1U;
    }
}

/* Keep both serial ports in byte-wise interrupt mode: USART3 for GNSS and
 * USART1 for the JDY-31 Bluetooth bridge. */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART3)
    {
        App_GpsHandleByte(g_gps_rx_byte);
        (void)HAL_UART_Receive_IT(&huart3, &g_gps_rx_byte, 1U);
    }
    else if (huart->Instance == USART1)
    {
        App_BtHandleByte(g_bt_rx_byte);
        (void)HAL_UART_Receive_IT(&huart1, &g_bt_rx_byte, 1U);
    }
}

/* Restart reception after UART overrun/line errors so the byte stream remains continuous. */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART3)
    {
        __HAL_UART_CLEAR_OREFLAG(&huart3);
        (void)HAL_UART_Receive_IT(&huart3, &g_gps_rx_byte, 1U);
    }
    else if (huart->Instance == USART1)
    {
        __HAL_UART_CLEAR_OREFLAG(&huart1);
        (void)HAL_UART_Receive_IT(&huart1, &g_bt_rx_byte, 1U);
    }
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  uint32_t now;
  uint8_t alert_active;

  /* Initialize the MCU, peripherals, and all application-side modules. */
  HAL_Init();
  SystemClock_Config();

  MX_GPIO_Init();
  MX_I2C1_Init();
  MX_USART1_UART_Init();
  MX_USART3_UART_Init();
  MX_TIM1_Init();
  MX_TIM3_Init();
  MX_I2C2_Init();
  MX_I2C3_Init();
  MX_TIM4_Init();
  MX_ADC1_Init();

  OLED_Init();
  App_BuzzerStop();

  (void)HAL_UART_Receive_IT(&huart3, &g_gps_rx_byte, 1U);
  (void)HAL_UART_Receive_IT(&huart1, &g_bt_rx_byte, 1U);

  Int_MAX30102_Init();

  if (mpu_dmp_init() == 0U)
  {
      g_mpu_basic_ready = 1U;
      g_mpu_dmp_ready = 1U;
  }
  else if (MPU_Init() == 0U)
  {
      g_mpu_basic_ready = 1U;
      g_mpu_dmp_ready = 0U;
  }

  g_boot_ms = HAL_GetTick();
  App_RtcTask();
  App_LightTask();
  App_DhtTask();
  g_ui_force_refresh = 1U;

  while (1)
  {
    now = HAL_GetTick();

    /* The firmware intentionally uses a simple cooperative scheduler instead of an RTOS. */
    App_GpsProcessPending();
    App_BtProcessPending();

    if ((now - g_last_key_ms) >= KEY_SCAN_MS)
    {
        g_last_key_ms = now;
        App_KeysTask();
    }

    if ((now - g_last_mpu_ms) >= MPU_UPDATE_MS)
    {
        g_last_mpu_ms = now;
        App_MotionTask();
    }

    if ((now - g_last_adc_ms) >= ADC_UPDATE_MS)
    {
        g_last_adc_ms = now;
        App_LightTask();
    }

    if ((now - g_last_rtc_ms) >= RTC_UPDATE_MS)
    {
        g_last_rtc_ms = now;
        App_RtcTask();
    }

    if ((now - g_last_dht_ms) >= DHT_UPDATE_MS)
    {
        g_last_dht_ms = now;
        App_DhtTask();
    }

    if (((now - g_boot_ms) >= MAX30102_STARTUP_DELAY_MS) && ((now - g_last_max30102_ms) >= MAX30102_UPDATE_MS))
    {
        g_last_max30102_ms = now;
        App_PulseTask();
    }

    /* Refresh the OLED either on demand (page/important state change) or at the regular UI cadence. */
    if ((g_ui_force_refresh != 0U) || ((now - g_last_oled_ms) >= OLED_UPDATE_MS))
    {
        g_last_oled_ms = now;
        g_ui_force_refresh = 0U;
        App_OledTask();
    }

    if ((now - g_last_bt_ms) >= BT_UPDATE_MS)
    {
        g_last_bt_ms = now;
        App_BluetoothTask();
    }

    /* A single alert flag keeps the buzzer logic independent from the individual modules. */
    alert_active = 0U;
    if ((g_dht.valid != 0U) && (g_dht.temp > g_ui.temp_warn))
    {
        alert_active = 1U;
    }
    if ((g_pulse.hr_valid != 0U) && (g_pulse.heart_rate > g_ui.hr_warn))
    {
        alert_active = 1U;
    }
    App_AlarmTask(now, alert_active);
  }
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure LSE Drive Capability
  */
  HAL_PWR_EnableBkUpAccess();
  __HAL_RCC_LSEDRIVE_CONFIG(RCC_LSEDRIVE_LOW);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_LSE|RCC_OSCILLATORTYPE_MSI;
  RCC_OscInitStruct.LSEState = RCC_LSE_ON;
  RCC_OscInitStruct.MSIState = RCC_MSI_ON;
  RCC_OscInitStruct.MSICalibrationValue = 0;
  RCC_OscInitStruct.MSIClockRange = RCC_MSIRANGE_6;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_MSI;
  RCC_OscInitStruct.PLL.PLLM = 1;
  RCC_OscInitStruct.PLL.PLLN = 36;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV7;
  RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
  RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
  {
    Error_Handler();
  }

  /** Enable MSI Auto calibration
  */
  HAL_RCCEx_EnableMSIPLLMode();
}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  __disable_irq();
  while (1)
  {
  }
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  (void)file;
  (void)line;
}
#endif /* USE_FULL_ASSERT */

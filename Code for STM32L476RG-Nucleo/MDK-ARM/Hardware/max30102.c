#include "max30102.h"
#include "i2c.h"
#include "stm32l4xx_hal_gpio.h"
#include "gpio.h"
#include <string.h>

/* MAX30102 processing is split into two stages:
 * 1) detect whether a finger is really present,
 * 2) only publish HR/SPO2 after a full and stable sample window. */

#define MAX30102_BUFFER_SIZE            BUFFER_SIZE
#define MAX30102_FIFO_READ_LIMIT        24U
#define MAX30102_RECALC_SAMPLES         100U
#define MAX30102_FINGER_WINDOW          12U
#define MAX30102_FINGER_ON_DC_MIN       1800U
#define MAX30102_FINGER_OFF_DC_MIN      900U
#define MAX30102_FINGER_AC_MIN          30U
#define MAX30102_SIGNAL_WINDOW          32U
#define MAX30102_SIGNAL_MIN             2500U
#define MAX30102_SIGNAL_AC_MIN          60U

static uint8_t fifo_data[6] = {0};
static uint16_t s_buffer_fill = 0U;
static uint16_t s_samples_since_calc = 0U;
static uint8_t s_part_ok = 0U;
static uint8_t s_finger_present = 0U;
static uint8_t s_finger_on_streak = 0U;
static uint8_t s_finger_off_streak = 0U;
static uint8_t s_result_updated = 0U;

uint32_t aun_ir_buffer[MAX30102_BUFFER_SIZE];
int32_t n_ir_buffer_length = MAX30102_BUFFER_SIZE;
uint32_t aun_red_buffer[MAX30102_BUFFER_SIZE];
int32_t n_sp02 = 0;
int8_t ch_spo2_valid = 0;
int32_t n_heart_rate = 0;
int8_t ch_hr_valid = 0;
uint8_t max30102_INT = 0U;

static void Int_MAX30102_SetOutputsInvalid(uint8_t mark_updated)
{
    n_sp02 = 0;
    ch_spo2_valid = 0;
    n_heart_rate = 0;
    ch_hr_valid = 0;
    if (mark_updated != 0U)
    {
        s_result_updated = 1U;
    }
}

static void Int_MAX30102_ClearBuffers(void)
{
    memset(aun_ir_buffer, 0, sizeof(aun_ir_buffer));
    memset(aun_red_buffer, 0, sizeof(aun_red_buffer));
    s_buffer_fill = 0U;
    s_samples_since_calc = 0U;
}

static void Int_MAX30102_HandleNoFinger(void)
{
    s_finger_present = 0U;
    s_finger_on_streak = 0U;
    s_finger_off_streak = 0U;
    Int_MAX30102_ClearBuffers();
    Int_MAX30102_SetOutputsInvalid(1U);
}

uint8_t Int_MAX30102_WriteByte(uint8_t reg_addr, uint8_t byte)
{
    return (HAL_I2C_Mem_Write(&hi2c1,
                              (uint16_t)(MAX30102_DEVICE_ADDR << 1),
                              reg_addr,
                              I2C_MEMADD_SIZE_8BIT,
                              &byte,
                              1U,
                              2000U) == HAL_OK) ? 0U : 1U;
}

uint8_t Int_MAX30102_ReadByte(uint8_t reg_addr, uint8_t *byte)
{
    return (HAL_I2C_Mem_Read(&hi2c1,
                             (uint16_t)(MAX30102_DEVICE_ADDR << 1),
                             reg_addr,
                             I2C_MEMADD_SIZE_8BIT,
                             byte,
                             1U,
                             2000U) == HAL_OK) ? 0U : 1U;
}

uint8_t Int_MAX30102_Read_Len(uint8_t reg_addr, uint8_t *data, uint8_t size)
{
    return (HAL_I2C_Mem_Read(&hi2c1,
                             (uint16_t)(MAX30102_DEVICE_ADDR << 1),
                             reg_addr,
                             I2C_MEMADD_SIZE_8BIT,
                             data,
                             size,
                             2000U) == HAL_OK) ? 0U : 1U;
}

static uint8_t Int_MAX30102_ReadFifoPointers(uint8_t *wr_ptr, uint8_t *rd_ptr, uint8_t *ovf)
{
    if ((wr_ptr == NULL) || (rd_ptr == NULL) || (ovf == NULL))
    {
        return 1U;
    }

    if (Int_MAX30102_ReadByte(REG_FIFO_WR_PTR, wr_ptr) != 0U)
    {
        return 1U;
    }
    if (Int_MAX30102_ReadByte(REG_FIFO_RD_PTR, rd_ptr) != 0U)
    {
        return 1U;
    }
    if (Int_MAX30102_ReadByte(REG_OVF_COUNTER, ovf) != 0U)
    {
        return 1U;
    }

    *wr_ptr &= 0x1FU;
    *rd_ptr &= 0x1FU;
    *ovf &= 0x1FU;
    return 0U;
}

static uint8_t Int_MAX30102_ClearInterrupts(void)
{
    uint8_t dummy;

    if (Int_MAX30102_ReadByte(REG_INTR_STATUS_1, &dummy) != 0U)
    {
        return 1U;
    }
    if (Int_MAX30102_ReadByte(REG_INTR_STATUS_2, &dummy) != 0U)
    {
        return 1U;
    }
    return 0U;
}

static uint8_t Int_MAX30102_ReadSample(uint32_t *red, uint32_t *ir)
{
    if ((red == NULL) || (ir == NULL))
    {
        return 1U;
    }

    if (Int_MAX30102_Read_Len(REG_FIFO_DATA, fifo_data, sizeof(fifo_data)) != 0U)
    {
        return 1U;
    }

    *red = (((uint32_t)(fifo_data[0] & 0x03U)) << 16)
         | (((uint32_t)fifo_data[1]) << 8)
         | ((uint32_t)fifo_data[2]);
    *ir  = (((uint32_t)(fifo_data[3] & 0x03U)) << 16)
         | (((uint32_t)fifo_data[4]) << 8)
         | ((uint32_t)fifo_data[5]);

    return 0U;
}

static uint8_t Int_MAX30102_AppendSamples(uint8_t sample_count)
{
    uint16_t shift;
    uint8_t i;
    uint32_t red;
    uint32_t ir;

    if (sample_count == 0U)
    {
        return 0U;
    }

    if (sample_count > MAX30102_FIFO_READ_LIMIT)
    {
        sample_count = MAX30102_FIFO_READ_LIMIT;
    }

    if ((uint16_t)(s_buffer_fill + sample_count) > MAX30102_BUFFER_SIZE)
    {
        shift = (uint16_t)(s_buffer_fill + sample_count - MAX30102_BUFFER_SIZE);
        if (shift > s_buffer_fill)
        {
            shift = s_buffer_fill;
        }
        memmove(aun_red_buffer,
                &aun_red_buffer[shift],
                (s_buffer_fill - shift) * sizeof(aun_red_buffer[0]));
        memmove(aun_ir_buffer,
                &aun_ir_buffer[shift],
                (s_buffer_fill - shift) * sizeof(aun_ir_buffer[0]));
        s_buffer_fill = (uint16_t)(s_buffer_fill - shift);
    }

    if (Int_MAX30102_ClearInterrupts() != 0U)
    {
        return 1U;
    }

    for (i = 0U; i < sample_count; i++)
    {
        if (Int_MAX30102_ReadSample(&red, &ir) != 0U)
        {
            return 1U;
        }
        aun_red_buffer[s_buffer_fill] = red;
        aun_ir_buffer[s_buffer_fill] = ir;
        s_buffer_fill++;
    }

    s_samples_since_calc = (uint16_t)(s_samples_since_calc + sample_count);
    return 0U;
}

/* Use recent DC and AC levels to debounce finger on/off decisions. */
static void Int_MAX30102_UpdateFingerPresence(void)
{
    uint16_t count;
    uint16_t start;
    uint16_t i;
    uint64_t ir_sum = 0U;
    uint64_t red_sum = 0U;
    uint32_t ir_min = 0xFFFFFFFFUL;
    uint32_t ir_max = 0U;
    uint32_t red_min = 0xFFFFFFFFUL;
    uint32_t red_max = 0U;
    uint32_t ir_pp;
    uint32_t red_pp;
    uint32_t ir_avg;
    uint32_t red_avg;

    if (s_buffer_fill < 4U)
    {
        return;
    }

    count = (s_buffer_fill >= MAX30102_FINGER_WINDOW) ? MAX30102_FINGER_WINDOW : s_buffer_fill;
    start = (uint16_t)(s_buffer_fill - count);

    for (i = start; i < s_buffer_fill; i++)
    {
        uint32_t ir = aun_ir_buffer[i];
        uint32_t red = aun_red_buffer[i];

        ir_sum += ir;
        red_sum += red;

        if (ir < ir_min)
        {
            ir_min = ir;
        }
        if (ir > ir_max)
        {
            ir_max = ir;
        }
        if (red < red_min)
        {
            red_min = red;
        }
        if (red > red_max)
        {
            red_max = red;
        }
    }

    ir_avg = (uint32_t)(ir_sum / count);
    red_avg = (uint32_t)(red_sum / count);
    ir_pp = ir_max - ir_min;
    red_pp = red_max - red_min;

    if (s_finger_present == 0U)
    {
        if ((ir_avg >= MAX30102_FINGER_ON_DC_MIN)
         && (red_avg >= MAX30102_FINGER_ON_DC_MIN)
         && ((ir_pp >= MAX30102_FINGER_AC_MIN) || (red_pp >= MAX30102_FINGER_AC_MIN)))
        {
            if (s_finger_on_streak < 3U)
            {
                s_finger_on_streak++;
            }
            if (s_finger_on_streak >= 2U)
            {
                s_finger_present = 1U;
                s_finger_off_streak = 0U;
                Int_MAX30102_SetOutputsInvalid(1U);
            }
        }
        else
        {
            s_finger_on_streak = 0U;
        }
    }
    else
    {
        if ((ir_avg < MAX30102_FINGER_OFF_DC_MIN)
         || (red_avg < MAX30102_FINGER_OFF_DC_MIN)
         || ((ir_pp < (MAX30102_FINGER_AC_MIN / 2U)) && (red_pp < (MAX30102_FINGER_AC_MIN / 2U))))
        {
            if (s_finger_off_streak < 3U)
            {
                s_finger_off_streak++;
            }
            if (s_finger_off_streak >= 2U)
            {
                Int_MAX30102_HandleNoFinger();
            }
        }
        else
        {
            s_finger_off_streak = 0U;
        }
    }
}

static uint8_t Int_MAX30102_SignalStrongEnough(void)
{
    uint16_t count;
    uint16_t start;
    uint16_t i;
    uint32_t ir_min = 0xFFFFFFFFUL;
    uint32_t ir_max = 0U;
    uint32_t red_min = 0xFFFFFFFFUL;
    uint32_t red_max = 0U;
    uint64_t ir_sum = 0U;
    uint64_t red_sum = 0U;
    uint32_t ir_pp;
    uint32_t red_pp;

    if (s_buffer_fill == 0U)
    {
        return 0U;
    }

    count = (s_buffer_fill >= MAX30102_SIGNAL_WINDOW) ? MAX30102_SIGNAL_WINDOW : s_buffer_fill;
    start = (uint16_t)(s_buffer_fill - count);

    for (i = start; i < s_buffer_fill; i++)
    {
        uint32_t ir = aun_ir_buffer[i];
        uint32_t red = aun_red_buffer[i];

        ir_sum += ir;
        red_sum += red;

        if (ir < ir_min)
        {
            ir_min = ir;
        }
        if (ir > ir_max)
        {
            ir_max = ir;
        }
        if (red < red_min)
        {
            red_min = red;
        }
        if (red > red_max)
        {
            red_max = red;
        }
    }

    ir_sum /= count;
    red_sum /= count;
    ir_pp = ir_max - ir_min;
    red_pp = red_max - red_min;

    return (((uint32_t)ir_sum >= MAX30102_SIGNAL_MIN)
         && ((uint32_t)red_sum >= MAX30102_SIGNAL_MIN)
         && (ir_pp >= MAX30102_SIGNAL_AC_MIN)
         && (red_pp >= MAX30102_SIGNAL_AC_MIN)) ? 1U : 0U;
}

void Int_MAX30102_Reset(void)
{
    (void)Int_MAX30102_WriteByte(REG_MODE_CONFIG, 0x40U);
    HAL_Delay(10U);
}

void Int_MAX30102_Init(void)
{
    uint8_t part_id = 0U;

    Int_MAX30102_ClearBuffers();
    max30102_INT = 0U;
    s_part_ok = 0U;
    s_finger_present = 0U;
    s_finger_on_streak = 0U;
    s_finger_off_streak = 0U;
    s_result_updated = 0U;
    Int_MAX30102_SetOutputsInvalid(0U);

    Int_MAX30102_Reset();
    (void)Int_MAX30102_ReadByte(REG_PART_ID, &part_id);
    if (part_id == 0x15U)
    {
        s_part_ok = 1U;
    }

    (void)Int_MAX30102_WriteByte(REG_INTR_ENABLE_1, 0xC0U);
    (void)Int_MAX30102_WriteByte(REG_INTR_ENABLE_2, 0x00U);
    (void)Int_MAX30102_WriteByte(REG_FIFO_WR_PTR, 0x00U);
    (void)Int_MAX30102_WriteByte(REG_OVF_COUNTER, 0x00U);
    (void)Int_MAX30102_WriteByte(REG_FIFO_RD_PTR, 0x00U);
    (void)Int_MAX30102_WriteByte(REG_FIFO_CONFIG, 0x0FU);
    (void)Int_MAX30102_WriteByte(REG_MODE_CONFIG, 0x03U);
    (void)Int_MAX30102_WriteByte(REG_SPO2_CONFIG, 0x27U);
    (void)Int_MAX30102_WriteByte(REG_LED1_PA, 0x35U);
    (void)Int_MAX30102_WriteByte(REG_LED2_PA, 0x35U);
    (void)Int_MAX30102_ClearInterrupts();
}

/* Pull fresh FIFO samples, refresh finger state, and run the Maxim algorithm
 * only when the rolling window is full enough for a stable estimate. */
void Int_MAX30102_GetSpo2AndHeartRate(void)
{
    uint8_t wr_ptr = 0U;
    uint8_t rd_ptr = 0U;
    uint8_t ovf = 0U;
    uint8_t available;

    if (s_part_ok == 0U)
    {
        Int_MAX30102_HandleNoFinger();
        return;
    }

    if (Int_MAX30102_ReadFifoPointers(&wr_ptr, &rd_ptr, &ovf) != 0U)
    {
        Int_MAX30102_HandleNoFinger();
        return;
    }

    available = (uint8_t)((wr_ptr - rd_ptr) & 0x1FU);

    if ((available == 0U) && (ovf == 0U) && (max30102_INT == 0U))
    {
        return;
    }

    if (ovf != 0U)
    {
        available = 16U;
    }

    if (Int_MAX30102_AppendSamples(available) != 0U)
    {
        Int_MAX30102_HandleNoFinger();
        return;
    }

    max30102_INT = 0U;
    Int_MAX30102_UpdateFingerPresence();

    if (s_finger_present == 0U)
    {
        return;
    }

    if (s_buffer_fill < MAX30102_BUFFER_SIZE)
    {
        return;
    }

    if (s_samples_since_calc < MAX30102_RECALC_SAMPLES)
    {
        return;
    }
    s_samples_since_calc = 0U;

    if (Int_MAX30102_SignalStrongEnough() == 0U)
    {
        Int_MAX30102_SetOutputsInvalid(1U);
        return;
    }

    maxim_heart_rate_and_oxygen_saturation(aun_ir_buffer,
                                           n_ir_buffer_length,
                                           aun_red_buffer,
                                           &n_sp02,
                                           &ch_spo2_valid,
                                           &n_heart_rate,
                                           &ch_hr_valid);
    s_result_updated = 1U;
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == MAX30102_INT_Pin)
    {
        max30102_INT = 1U;
    }
}

uint8_t Int_MAX30102_FingerDetected(void)
{
    return s_finger_present;
}

uint8_t Int_MAX30102_ResultUpdated(void)
{
    uint8_t updated = s_result_updated;
    s_result_updated = 0U;
    return updated;
}

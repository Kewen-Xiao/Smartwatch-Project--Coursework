/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : DS3231.c
  * @brief          : DS3231 program body
  ******************************************************************************
  */
/* USER CODE END Header */

#include "DS3231.h"
#include "i2c.h"

/* Minimal DS3231 wrapper used by main.c to read and write calendar values over I2C. */

static uint8_t DS3231Buffer[1];
static uint8_t DS3231TimeBuffer[8];

/* Convert a packed BCD register value into a normal decimal number. */
uint8_t BCD_DEC(uint8_t BCD_Data)
{
    return (uint8_t)(((BCD_Data >> 4) * 10U) + (BCD_Data & 0x0FU));
}

/* Convert a decimal number back into the DS3231 register format. */
uint8_t DEC_BCD(uint8_t DEC_Data)
{
    return (uint8_t)(((DEC_Data / 10U) << 4) | (DEC_Data % 10U));
}

uint8_t I2C_DS3231_ReadData(uint8_t ReadAddr)
{
    if (HAL_I2C_Mem_Read(&hi2c3,
                         DS3231_ADDRESS,
                         ReadAddr,
                         I2C_MEMADD_SIZE_8BIT,
                         DS3231Buffer,
                         sizeof(DS3231Buffer),
                         1000U) != HAL_OK)
    {
        return 1U;
    }
    return 0U;
}

uint8_t I2C_DS3231_WriteData(uint8_t WriteAddr, uint8_t Data)
{
    uint8_t value = Data;

    if (HAL_I2C_Mem_Write(&hi2c3,
                          DS3231_ADDRESS,
                          WriteAddr,
                          I2C_MEMADD_SIZE_8BIT,
                          &value,
                          1U,
                          1000U) != HAL_OK)
    {
        return 1U;
    }
    return 0U;
}

/* Write the full date/time set into the RTC. */
void DS3231_SetTime(uint8_t Yea, uint8_t Mon, uint8_t Dat, uint8_t Wee, uint8_t Hou, uint8_t Min, uint8_t Sec)
{
    (void)I2C_DS3231_WriteData(Year_Register, DEC_BCD(Yea));
    (void)I2C_DS3231_WriteData(Month_Register, DEC_BCD(Mon));
    (void)I2C_DS3231_WriteData(Date_Register, DEC_BCD(Dat));
    (void)I2C_DS3231_WriteData(Day_Register, DEC_BCD(Wee));
    (void)I2C_DS3231_WriteData(Hour_Register, DEC_BCD(Hou));
    (void)I2C_DS3231_WriteData(Minutes_Register, DEC_BCD(Min));
    (void)I2C_DS3231_WriteData(Seconds_Register, DEC_BCD(Sec));
}

/* Read the current clock registers into a small shared buffer. */
void DS3231_ReadTime(void)
{
    uint8_t raw_time[7];

    if (HAL_I2C_Mem_Read(&hi2c3,
                         DS3231_ADDRESS,
                         Seconds_Register,
                         I2C_MEMADD_SIZE_8BIT,
                         raw_time,
                         sizeof(raw_time),
                         1000U) != HAL_OK)
    {
        return;
    }

    DS3231TimeBuffer[7] = BCD_DEC((uint8_t)(raw_time[0] & 0x7FU));
    DS3231TimeBuffer[6] = BCD_DEC((uint8_t)(raw_time[1] & 0x7FU));
    DS3231TimeBuffer[5] = BCD_DEC((uint8_t)(raw_time[2] & 0x3FU));
    DS3231TimeBuffer[4] = BCD_DEC((uint8_t)(raw_time[3] & 0x07U));
    DS3231TimeBuffer[3] = BCD_DEC((uint8_t)(raw_time[4] & 0x3FU));
    DS3231TimeBuffer[2] = BCD_DEC((uint8_t)(raw_time[5] & 0x1FU));
    DS3231TimeBuffer[1] = BCD_DEC(raw_time[6]);
}

/* Return the cached time buffer so higher-level code can copy fields out of it. */
uint8_t *DS3231_ReadTime_ReturnPoint(void)
{
    return DS3231TimeBuffer;
}

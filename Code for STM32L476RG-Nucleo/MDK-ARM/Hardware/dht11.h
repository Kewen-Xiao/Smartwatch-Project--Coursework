#ifndef _DH11_H_
#define _DH11_H_

#include "main.h"

#define u8 unsigned char
#define u16 unsigned short
#define u32 unsigned int

#define DATA_SET()   HAL_GPIO_WritePin(DATA_GPIO_Port, DATA_Pin, GPIO_PIN_SET)
#define DATA_RESET() HAL_GPIO_WritePin(DATA_GPIO_Port, DATA_Pin, GPIO_PIN_RESET)
#define DATA_READ()  HAL_GPIO_ReadPin(DATA_GPIO_Port, DATA_Pin)

typedef struct
{
  u8 Data[5];      // Raw bytes returned by the sensor.
  u8 index;        // Rolling sample index used by legacy code paths.
  u8 temp;         // Last valid temperature reading in degrees Celsius.
  u8 humidity;     // Last valid humidity reading in percent.
} DH11_DATA;

extern DH11_DATA DH11_data;

u8 DH11_Read(void);
void DH11_Task(void);

#define DATA_Pin GPIO_PIN_2
#define DATA_GPIO_Port GPIOC

#endif

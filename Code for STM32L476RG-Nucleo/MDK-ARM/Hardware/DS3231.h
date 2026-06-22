/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : DS3231.h
  * @brief          : Header for DS3231.c file.
  *                   This file provides code for the configuration
  *                   of the DS3231 instances
  * @author         : Lesterbor
  *	@time			:	2021-09-26
  ******************************************************************************
  * @attention
  *
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __DS3231_H_
#define __DS3231_H_
/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

	#include "main.h"
	
/* USER CODE END Includes */
	
/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PT */



	#define 	I2C_WR					0x00		// I2C write direction bit
	#define 	I2C_RD					0x01		// I2C read direction bit
	#define 	DS3231_ADDRESS 			0xD0   		// 8-bit DS3231 I2C address
	
	#define 	Seconds_Register      	0x00     	// Seconds register
	#define 	Minutes_Register		0x01     	// Minutes register
	#define 	Hour_Register  			0x02     	// Hours register
	#define 	Day_Register       		0x03     	// Day-of-week register
	#define 	Date_Register      		0x04		// Day-of-month register
	#define	 	Month_Register     		0x05    	// Month register
	#define 	Year_Register       	0x06    	// Year register (00-99)

/* USER CODE END PT */
	
/* Exported functions prototypes ---------------------------------------------*/
/* USER CODE BEGIN EFP */

	uint8_t BCD_DEC(uint8_t BCD_Data);
	uint8_t DEC_BCD(uint8_t DEC_Data);
	uint8_t I2C_DS3231_ReadData(uint8_t ReadAddr);
	uint8_t I2C_DS3231_WriteData(uint8_t WriteAddr,uint8_t Data);
	void DS3231_SetTime(uint8_t Yea,uint8_t Mon,uint8_t Dat,uint8_t Wee,uint8_t Hou,uint8_t Min,uint8_t Sec);
	void DS3231_ReadTime(void);
	uint8_t *DS3231_ReadTime_ReturnPoint(void);

/* USER CODE END EFP */

#endif /* __DS3231_H_ */
/************************ (C) COPYRIGHT Lesterbor *****END OF FILE*************/


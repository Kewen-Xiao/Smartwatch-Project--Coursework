#ifndef __MPU6050_H
#define __MPU6050_H
#include "IIC.h"


#define delay_ms				HAL_Delay
#define MPU_IIC_Init			IIC_GPIO_Init
#define MPU_IIC_Start			IIC_Start
#define MPU_IIC_Stop			IIC_Stop
#define MPU_IIC_Send_Byte		IIC_Send_Byte
#define MPU_IIC_Read_Byte		IIC_Read_Byte
#define MPU_IIC_Wait_Ack		IIC_Wait_Ack

//#define MPU_ACCEL_OFFS_REG		0X06	//accel_offs,,
//#define MPU_PROD_ID_REG			0X0C	//prod id,
#define MPU_SELF_TESTX_REG		0X0D	//X
#define MPU_SELF_TESTY_REG		0X0E	//Y
#define MPU_SELF_TESTZ_REG		0X0F	//Z
#define MPU_SELF_TESTA_REG		0X10	//A
#define MPU_SAMPLE_RATE_REG		0X19	//
#define MPU_CFG_REG				0X1A	//
#define MPU_GYRO_CFG_REG			0X1B	//
#define MPU_ACCEL_CFG_REG			0X1C	//
#define MPU_MOTION_DET_REG		0X1F	//
#define MPU_FIFO_EN_REG			0X23	//FIFO
#define MPU_I2CMST_CTRL_REG		0X24	//IIC
#define MPU_I2CSLV0_ADDR_REG		0X25	//IIC0
#define MPU_I2CSLV0_REG			0X26	//IIC0
#define MPU_I2CSLV0_CTRL_REG		0X27	//IIC0
#define MPU_I2CSLV1_ADDR_REG		0X28	//IIC1
#define MPU_I2CSLV1_REG			0X29	//IIC1
#define MPU_I2CSLV1_CTRL_REG		0X2A	//IIC1
#define MPU_I2CSLV2_ADDR_REG		0X2B	//IIC2
#define MPU_I2CSLV2_REG			0X2C	//IIC2
#define MPU_I2CSLV2_CTRL_REG		0X2D	//IIC2
#define MPU_I2CSLV3_ADDR_REG		0X2E	//IIC3
#define MPU_I2CSLV3_REG			0X2F	//IIC3
#define MPU_I2CSLV3_CTRL_REG		0X30	//IIC3
#define MPU_I2CSLV4_ADDR_REG		0X31	//IIC4
#define MPU_I2CSLV4_REG			0X32	//IIC4
#define MPU_I2CSLV4_DO_REG		0X33	//IIC4
#define MPU_I2CSLV4_CTRL_REG		0X34	//IIC4
#define MPU_I2CSLV4_DI_REG		0X35	//IIC4

#define MPU_I2CMST_STA_REG		0X36	//IIC
#define MPU_INTBP_CFG_REG			0X37	///
#define MPU_INT_EN_REG			0X38	//
#define MPU_INT_STA_REG			0X3A	//

#define MPU_ACCEL_XOUTH_REG		0X3B	//,X8
#define MPU_ACCEL_XOUTL_REG		0X3C	//,X8
#define MPU_ACCEL_YOUTH_REG		0X3D	//,Y8
#define MPU_ACCEL_YOUTL_REG		0X3E	//,Y8
#define MPU_ACCEL_ZOUTH_REG		0X3F	//,Z8
#define MPU_ACCEL_ZOUTL_REG		0X40	//,Z8

#define MPU_TEMP_OUTH_REG			0X41	//
#define MPU_TEMP_OUTL_REG			0X42	//8

#define MPU_GYRO_XOUTH_REG		0X43	//,X8
#define MPU_GYRO_XOUTL_REG		0X44	//,X8
#define MPU_GYRO_YOUTH_REG		0X45	//,Y8
#define MPU_GYRO_YOUTL_REG		0X46	//,Y8
#define MPU_GYRO_ZOUTH_REG		0X47	//,Z8
#define MPU_GYRO_ZOUTL_REG		0X48	//,Z8

#define MPU_I2CSLV0_DO_REG		0X63	//IIC0
#define MPU_I2CSLV1_DO_REG		0X64	//IIC1
#define MPU_I2CSLV2_DO_REG		0X65	//IIC2
#define MPU_I2CSLV3_DO_REG		0X66	//IIC3

#define MPU_I2CMST_DELAY_REG		0X67	//IIC
#define MPU_SIGPATH_RST_REG		0X68	//
#define MPU_MDETECT_CTRL_REG		0X69	//
#define MPU_USER_CTRL_REG			0X6A	//
#define MPU_PWR_MGMT1_REG			0X6B	//1
#define MPU_PWR_MGMT2_REG			0X6C	//2 
#define MPU_FIFO_CNTH_REG			0X72	//FIFO
#define MPU_FIFO_CNTL_REG			0X73	//FIFO
#define MPU_FIFO_RW_REG			0X74	//FIFO
#define MPU_DEVICE_ID_REG			0X75	//ID

//AD0(9),IIC0X68().
//V3.3,IIC0X69().
#define MPU_ADDR					0X68


////AD0GND,,0XD10XD0(VCC,0XD30XD2)
//#define MPU_READ    0XD1
//#define MPU_WRITE   0XD0

uint8_t MPU_Init(void); 								//MPU6050
uint8_t MPU_Write_Len(uint8_t addr,uint8_t reg,uint8_t len,uint8_t *buf);//IIC
uint8_t MPU_Read_Len(uint8_t addr,uint8_t reg,uint8_t len,uint8_t *buf); //IIC
uint8_t MPU_Write_Byte(uint8_t reg,uint8_t data);				//IIC
uint8_t MPU_Read_Byte(uint8_t reg);						//IIC

uint8_t MPU_Set_Gyro_Fsr(uint8_t fsr);
uint8_t MPU_Set_Accel_Fsr(uint8_t fsr);
uint8_t MPU_Set_LPF(uint16_t lpf);
uint8_t MPU_Set_Rate(uint16_t rate);
uint8_t MPU_Set_Fifo(uint8_t sens);


short MPU_Get_Temperature(void);
uint8_t MPU_Get_Gyroscope(short *gx,short *gy,short *gz);
uint8_t MPU_Get_Accelerometer(short *ax,short *ay,short *az);

#endif

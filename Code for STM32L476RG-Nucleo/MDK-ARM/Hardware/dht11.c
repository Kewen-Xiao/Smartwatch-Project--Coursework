#include "dht11.h"

/* Bit-banged DHT11 driver using TIM4 as a microsecond delay source. */
 
extern TIM_HandleTypeDef htim4;
 
static void DATA_OUTPUT(u8 flg); // Drive the single-wire bus from the MCU side.
static u8 DATA_INPUT(void); // Release the bus and sample its logic level.
static u8 DH11_Read_Byte(void); // Receive one data byte from the sensor.
 
u8 DH11_Read(void); // High-level read API used by the application.
 
static void Test(void); // Small wrapper kept for legacy task-style code.
 
DH11_DATA DH11_data;
 
/* Busy-wait delay based on TIM4 so DHT11 pulse widths can be measured in microseconds. */
void Delay_us(uint16_t us)
{
	uint16_t differ = 0xffff-us-5;				
	__HAL_TIM_SET_COUNTER(&htim4,differ);	//TIM1
	HAL_TIM_Base_Start(&htim4);		//	
	
	while(differ < 0xffff-5){	//
		differ = __HAL_TIM_GET_COUNTER(&htim4);		//
	}
	HAL_TIM_Base_Stop(&htim4);
}
 
 
/* Configure the DHT11 data pin as push-pull output and drive it high or low. */
void DATA_OUTPUT(u8 flg)
{
  	GPIO_InitTypeDef GPIO_InitStruct = {0};
	
	GPIO_InitStruct.Pin = DATA_Pin;
	GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
	GPIO_InitStruct.Pull = GPIO_NOPULL;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
	HAL_GPIO_Init(DATA_GPIO_Port, &GPIO_InitStruct);
 
	if(flg==0)
	{
		DATA_RESET();
	}
	else
	{
		DATA_SET();
	}
}
 
/* Configure the DHT11 data pin as input and return the sampled level. */
u8 DATA_INPUT(void)
{
  	GPIO_InitTypeDef GPIO_InitStruct = {0};
	u8 flg=0;
	
	GPIO_InitStruct.Pin = DATA_Pin;
  	GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  	GPIO_InitStruct.Pull = GPIO_PULLUP;
  	HAL_GPIO_Init(DATA_GPIO_Port, &GPIO_InitStruct);
 
	if(DATA_READ()==GPIO_PIN_RESET)
	{
		flg=0;
	}
	else 
	{
		flg=1;
	}
 
	return flg;
}
 
/* Decode eight pulse-width encoded bits from the DHT11. */
u8 DH11_Read_Byte(void)
{
    u8 ReadDat=0;
    u8 temp=0;
    u8 retry=0;
    u8 i=0;
    
    for(i=0;i<8;i++)
    {
      while(DATA_READ()==0&&retry<100)//DHT11
      {
        Delay_us(1);
        retry++;
      }
      retry=0;
      Delay_us(40);
      if(DATA_READ()==1)
      {
        temp=1;
      }
      else
      {
        temp=0;
      }
      while(DATA_READ()==1&&retry<100)//DHT111bit
      {
        Delay_us(1);
        retry++;
      }
      retry=0;
      ReadDat<<=1;
      ReadDat|=temp;
    }
    
    return ReadDat;
}
 
/* Execute the full DHT11 start sequence, read five bytes, and verify the checksum. */
u8 DH11_Read(void)
{
  u8 retry=0;
  u8 i=0;
  
  DATA_OUTPUT(0);//MCUDH11
  HAL_Delay(18);
  DATA_SET();
  Delay_us(20);
  
  DATA_INPUT();//DH11MCU
  Delay_us(20);
  if(DATA_READ()==0)
  {
    while(DATA_READ()==0&&retry<100)
    {
      Delay_us(1);
      retry++;
    }
    retry=0;
    while(DATA_READ()==1&&retry<100)
    {
      Delay_us(1);
      retry++;
    }
    retry=0;
    
    for(i=0;i<5;i++)//Data[0] Data[2]Data[1]Data[3]02Data[4]
    {
      DH11_data.Data[i]=DH11_Read_Byte();
    }
    Delay_us(50);
  }
  u32 sum=DH11_data.Data[0]+DH11_data.Data[1]+DH11_data.Data[2]+DH11_data.Data[3];//
  if((sum)==DH11_data.Data[4])
  {
    DH11_data.humidity=DH11_data.Data[0];//
    DH11_data.temp=DH11_data.Data[2];//
    return 1;    
  }
  else
  {
    return 0;
  }
}
 
/* Legacy helper that simply performs one read and advances the sample index. */
void Test(void)
{
  if(DH11_Read())
  {
    DH11_data.index++;
    if(DH11_data.index>=128)
    {
      DH11_data.index=0;
    }
  }
   
}
 
 
/* Optional task-style wrapper kept for compatibility with older code. */
void DH11_Task(void)
{
     Test();
}


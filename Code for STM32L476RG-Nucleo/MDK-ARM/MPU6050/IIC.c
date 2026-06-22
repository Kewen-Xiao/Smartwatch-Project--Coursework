#include "stm32l4xx_hal.h"
#include "IIC.h"

/* IICGPIO, 4SCLSDA */
#define GPIO_PORT_IIC     GPIOC                    /* GPIO */
#define RCC_IIC_ENABLE    __HAL_RCC_GPIOC_CLK_ENABLE()       /* GPIO */
#define IIC_SCL_PIN       GPIO_PIN_8                  /* SCLGPIO */
#define IIC_SDA_PIN       GPIO_PIN_9                  /* SDAGPIO */

/* SCLSDA */
#if 1	/*  1 GPIOIO */
#define IIC_SCL_1()  HAL_GPIO_WritePin(GPIO_PORT_IIC, IIC_SCL_PIN, GPIO_PIN_SET)		/* SCL = 1 */
#define IIC_SCL_0()  HAL_GPIO_WritePin(GPIO_PORT_IIC, IIC_SCL_PIN, GPIO_PIN_RESET)		/* SCL = 0 */

#define IIC_SDA_1()  HAL_GPIO_WritePin(GPIO_PORT_IIC, IIC_SDA_PIN, GPIO_PIN_SET)		/* SDA = 1 */
#define IIC_SDA_0()  HAL_GPIO_WritePin(GPIO_PORT_IIC, IIC_SDA_PIN, GPIO_PIN_RESET)		/* SDA = 0 */

#define IIC_SDA_READ()  HAL_GPIO_ReadPin(GPIO_PORT_IIC, IIC_SDA_PIN)	/* SDA */
#else	/* IO */
/*IAR */
#define IIC_SCL_1()  GPIO_PORT_IIC->BSRR = IIC_SCL_PIN				/* SCL = 1 */
#define IIC_SCL_0()  GPIO_PORT_IIC->BRR = IIC_SCL_PIN				/* SCL = 0 */

#define IIC_SDA_1()  GPIO_PORT_IIC->BSRR = IIC_SDA_PIN				/* SDA = 1 */
#define IIC_SDA_0()  GPIO_PORT_IIC->BRR = IIC_SDA_PIN				/* SDA = 0 */

#define IIC_SDA_READ()  ((GPIO_PORT_IIC->IDR & IIC_SDA_PIN) != 0)	/* SDA */
#endif

void IIC_GPIO_Init(void);

/*
*********************************************************************************************************
*	  : IIC_Delay
*	: IIC400KHz
*	    
*	  : 
*********************************************************************************************************
*/
static void IIC_Delay(void)
{
    uint8_t i;

    /*
     	AX-Pro
    	CPU72MHzFlash, MDK
    	10SCL = 205KHz
    	7SCL = 347KHz SCL1.5usSCL2.87us
     	5SCL = 421KHz SCL1.25usSCL2.375us

    IAR7
    */
    for (i = 0; i < 18; i++)
    {
        __NOP();
    }
}

/*
*********************************************************************************************************
*	  : IIC_Start
*	: CPUIIC
*	    
*	  : 
*********************************************************************************************************
*/
void IIC_Start(void)
{
    /* SCLSDAIIC */
    IIC_SDA_1();
    IIC_SCL_1();
    IIC_Delay();
    IIC_SDA_0();
    IIC_Delay();
    IIC_SCL_0();
    IIC_Delay();
}

/*
*********************************************************************************************************
*	  : IIC_Start
*	: CPUIIC
*	    
*	  : 
*********************************************************************************************************
*/
void IIC_Stop(void)
{
    /* SCLSDAIIC */
    IIC_SDA_0();
    IIC_SCL_1();
    IIC_Delay();
    IIC_SDA_1();
}

/*
*********************************************************************************************************
*	  : IIC_SendByte
*	: CPUIIC8bit
*	    _ucByte  
*	  : 
*********************************************************************************************************
*/
void IIC_Send_Byte(uint8_t _ucByte)
{
    uint8_t i;

    /* bit7 */
    for (i = 0; i < 8; i++)
    {
        if (_ucByte & 0x80)
        {
            IIC_SDA_1();
        }
        else
        {
            IIC_SDA_0();
        }
        IIC_Delay();
        IIC_SCL_1();
        IIC_Delay();
        IIC_SCL_0();
        if (i == 7)
        {
            IIC_SDA_1(); // 
        }
        _ucByte <<= 1;	/* bit */
        IIC_Delay();
    }
}

/*
*********************************************************************************************************
*	  : IIC_ReadByte
*	: CPUIIC8bit
*	    
*	  : 
*********************************************************************************************************
*/
uint8_t IIC_Read_Byte(uint8_t ack)
{
    uint8_t i;
    uint8_t value;

    /* 1bitbit7 */
    value = 0;
    for (i = 0; i < 8; i++)
    {
        value <<= 1;
        IIC_SCL_1();
        IIC_Delay();
        if (IIC_SDA_READ())
        {
            value++;
        }
        IIC_SCL_0();
        IIC_Delay();
    }
    if(ack==0)
        IIC_NAck();
    else
        IIC_Ack();
    return value;
}

/*
*********************************************************************************************************
*	  : IIC_WaitAck
*	: CPUACK
*	    
*	  : 01
*********************************************************************************************************
*/
uint8_t IIC_Wait_Ack(void)
{
    uint8_t re;

    IIC_SDA_1();	/* CPUSDA */
    IIC_Delay();
    IIC_SCL_1();	/* CPUSCL = 1, ACK */
    IIC_Delay();
    if (IIC_SDA_READ())	/* CPUSDA */
    {
        re = 1;
    }
    else
    {
        re = 0;
    }
    IIC_SCL_0();
    IIC_Delay();
    return re;
}

/*
*********************************************************************************************************
*	  : IIC_Ack
*	: CPUACK
*	    
*	  : 
*********************************************************************************************************
*/
void IIC_Ack(void)
{
    IIC_SDA_0();	/* CPUSDA = 0 */
    IIC_Delay();
    IIC_SCL_1();	/* CPU1 */
    IIC_Delay();
    IIC_SCL_0();
    IIC_Delay();
    IIC_SDA_1();	/* CPUSDA */
}

/*
*********************************************************************************************************
*	  : IIC_NAck
*	: CPU1NACK
*	    
*	  : 
*********************************************************************************************************
*/
void IIC_NAck(void)
{
    IIC_SDA_1();	/* CPUSDA = 1 */
    IIC_Delay();
    IIC_SCL_1();	/* CPU1 */
    IIC_Delay();
    IIC_SCL_0();
    IIC_Delay();
}

/*
*********************************************************************************************************
*	  : IIC_GPIO_Config
*	: IICGPIOIO
*	    
*	  : 
*********************************************************************************************************
*/
void IIC_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};

    RCC_IIC_ENABLE;	/* GPIO clock enable */

    GPIO_InitStructure.Pin = IIC_SCL_PIN | IIC_SDA_PIN;
    GPIO_InitStructure.Mode = GPIO_MODE_OUTPUT_OD;	/* open-drain output */
    GPIO_InitStructure.Pull = GPIO_PULLUP;
    GPIO_InitStructure.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    HAL_GPIO_Init(GPIO_PORT_IIC, &GPIO_InitStructure);

    IIC_SDA_1();
    IIC_SCL_1();
    IIC_Delay();
    IIC_Stop();
}

/*
*********************************************************************************************************
*	  : IIC_CheckDevice
*	: IICCPU
*	    _AddressIIC
*	  :  0  1
*********************************************************************************************************
*/
uint8_t IIC_CheckDevice(uint8_t _Address)
{
    uint8_t ucAck;

    IIC_GPIO_Init();		/* GPIO */

    IIC_Start();		/*  */

    /* +bit0 = w 1 = r) bit7  */
    IIC_Send_Byte(_Address|IIC_WR);
    ucAck = IIC_Wait_Ack();	/* ACK */

    IIC_Stop();			/*  */

    return ucAck;
}

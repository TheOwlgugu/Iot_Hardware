#include "OneWire.h"
#include "Delay.h"

#define DQ_PORT   GPIOA
#define DQ_PIN    GPIO_Pin_1

#define DQ_LOW()   GPIO_ResetBits(DQ_PORT, DQ_PIN)
#define DQ_HIGH()  GPIO_SetBits(DQ_PORT, DQ_PIN)
#define DQ_READ()  GPIO_ReadInputDataBit(DQ_PORT, DQ_PIN)

/**
  * @brief  1-Wire 初始化：PA1 开漏输出，释放总线
  * @note   开漏输出模式下读 IDR 仍然能读到引脚实际电平
  */
void OneWire_Init(void)
{
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);

    GPIO_InitTypeDef GPIO_InitStructure;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_Out_OD;
    GPIO_InitStructure.GPIO_Pin   = DQ_PIN;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(DQ_PORT, &GPIO_InitStructure);

    DQ_HIGH();
    Delay_ms(10);
}

/**
  * @brief  复位脉冲 + 存在脉冲检测
  * @retval 1=检测到器件，0=没有
  */
uint8_t OneWire_Reset(void)
{
    uint8_t presence;

    DQ_LOW();
    Delay_us(480);

    DQ_HIGH();
    Delay_us(70);

    presence = (DQ_READ() == 0) ? 1 : 0;

    Delay_us(410);
    return presence;
}

/**
  * @brief  写一个位（LSB 先发）
  */
static void OneWire_WriteBit(uint8_t bit)
{
    DQ_LOW();

    if (bit)
    {
        Delay_us(6);
        DQ_HIGH();
        Delay_us(64);
    }
    else
    {
        Delay_us(60);
        DQ_HIGH();
        Delay_us(10);
    }
}

/**
  * @brief  写一个字节
  */
void OneWire_WriteByte(uint8_t byte)
{
    uint8_t i;

    for (i = 0; i < 8; i++)
    {
        OneWire_WriteBit(byte & 0x01);
        byte >>= 1;
    }
}

/**
  * @brief  读一个位
  */
static uint8_t OneWire_ReadBit(void)
{
    uint8_t bit;

    DQ_LOW();
    Delay_us(2);
    DQ_HIGH();
    Delay_us(10);

    bit = (DQ_READ() != 0) ? 1 : 0;

    Delay_us(50);
    return bit;
}

/**
  * @brief  读一个字节（LSB 先收）
  */
uint8_t OneWire_ReadByte(void)
{
    uint8_t i;
    uint8_t data = 0;

    for (i = 0; i < 8; i++)
    {
        if (OneWire_ReadBit()) data |= (uint8_t)(1 << i);
    }

    return data;
}

/**
  * @brief  Dallas/Maxim CRC8（反射多项式 0x8C）
  */
uint8_t OneWire_Crc8(const uint8_t *data, uint8_t len)
{
    uint8_t crc = 0;
    uint8_t i, b;

    for (i = 0; i < len; i++)
    {
        crc ^= data[i];
        for (b = 0; b < 8; b++)
        {
            if (crc & 0x01) crc = (uint8_t)((crc >> 1) ^ 0x8C);
            else            crc = (uint8_t)(crc >> 1);
        }
    }

    return crc;
}

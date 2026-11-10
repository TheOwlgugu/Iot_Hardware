#include "Power.h"
#include "stm32f10x.h"

/**
  * @brief  电源开关引脚初始化（PB0/PB1，默认关断）
  */
void Power_Rail_Init(void)
{
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);

    GPIO_InitTypeDef GPIO_InitStructure;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Pin   = SENSOR_PWR_PIN | LORA_PWR_PIN;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    Power_Sensor_OFF();
    Power_Lora_OFF();
}

void Power_Sensor_ON(void)
{
    GPIO_ResetBits(SENSOR_PWR_PORT, SENSOR_PWR_PIN);
}

void Power_Sensor_OFF(void)
{
    GPIO_SetBits(SENSOR_PWR_PORT, SENSOR_PWR_PIN);
}

void Power_Lora_ON(void)
{
    GPIO_ResetBits(LORA_PWR_PORT, LORA_PWR_PIN);
}

void Power_Lora_OFF(void)
{
    GPIO_SetBits(LORA_PWR_PORT, LORA_PWR_PIN);
}

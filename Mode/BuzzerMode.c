#include "BuzzerMode.h"
#include "Delay.h"

/**
  * @brief  蜂鸣器初始化：PA1 推挽输出，默认高电平（关闭）
  */
void BuzzerMode_Init(void)
{
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);

    GPIO_InitTypeDef GPIO_InitStructure;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_1;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    BuzzerMode_Off();
}

/**
  * @brief  打开蜂鸣器（低电平触发）
  */
void BuzzerMode_On(void)
{
    GPIO_ResetBits(GPIOA, GPIO_Pin_1);
}

/**
  * @brief  关闭蜂鸣器
  */
void BuzzerMode_Off(void)
{
    GPIO_SetBits(GPIOA, GPIO_Pin_1);
}

/**
  * @brief  响一声
  * @param  ms  持续时间（毫秒）
  */
void BuzzerMode_Beep(uint16_t ms)
{
    BuzzerMode_On();
    Delay_ms(ms);
    BuzzerMode_Off();
}

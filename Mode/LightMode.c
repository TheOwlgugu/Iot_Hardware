#include "LightMode.h"

/**
  * @brief  光照电平变化标志（EXTI1 中断置 1，主循环消费）
  * @note   中断里只置标志，不做串口/网络操作
  */
static volatile uint8_t light_edge = 0;

/**
  * @brief  光照传感器初始化：PA1 上拉输入 + EXTI1 双边沿中断
  * @note   双边沿是为了"遮光"和"见光"都能立刻唤醒；
  *         EXTI 可以在 STOP 模式下把 MCU 唤醒，所以响应不受 10 秒轮询影响
  */
void LightMode_Init(void)
{
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO, ENABLE);   /* EXTI 线映射需要 AFIO */

    GPIO_InitTypeDef GPIO_InitStructure;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_IPU;
    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_1;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    /* PA1 -> EXTI1 */
    GPIO_EXTILineConfig(GPIO_PortSourceGPIOA, GPIO_PinSource1);

    EXTI_InitTypeDef EXTI_InitStructure;
    EXTI_InitStructure.EXTI_Line    = EXTI_Line1;
    EXTI_InitStructure.EXTI_Mode    = EXTI_Mode_Interrupt;
    EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Rising_Falling;
    EXTI_InitStructure.EXTI_LineCmd = ENABLE;
    EXTI_Init(&EXTI_InitStructure);

    NVIC_InitTypeDef NVIC_InitStructure;
    NVIC_InitStructure.NVIC_IRQChannel                   = EXTI1_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 2;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority        = 2;
    NVIC_Init(&NVIC_InitStructure);

    EXTI_ClearITPendingBit(EXTI_Line1);
}

/**
  * @brief  读取光照 DO
  * @retval 0/1
  */
uint8_t LightMode_Read(void)
{
    return GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_1);
}

uint8_t LightMode_EdgePending(void)
{
    return light_edge;
}

void LightMode_ClearEdge(void)
{
    light_edge = 0;
}

/**
  * @brief  EXTI1 中断：PA1 电平变化（遮光/见光）
  * @note   只置标志，发送放到主循环，避免在中断里做耗时操作
  */
void EXTI1_IRQHandler(void)
{
    if (EXTI_GetITStatus(EXTI_Line1) != RESET)
    {
        EXTI_ClearITPendingBit(EXTI_Line1);
        light_edge = 1;
    }
}

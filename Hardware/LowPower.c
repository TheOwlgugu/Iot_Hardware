#include "LowPower.h"
#include "stm32f10x.h"
#include "Delay.h"
#include "Millis.h"
#include "Serial3.h"

/* RTC 是否可用（LSI 起振且寄存器同步成功才为 1）。不可用时休眠退化为普通延时 */
static uint8_t rtc_ready = 0;

/**
  * @brief  等待 RTC 寄存器与 APB1 同步（RSF 置位），带超时
  */
static uint8_t RTC_WaitSync(void)
{
    uint32_t timeout = 0x1000000;
    while (RTC_GetFlagStatus(RTC_FLAG_RSF) == RESET)
    {
        if (--timeout == 0) return 0;
    }
    return 1;
}

/**
  * @brief  RTC 初始化（LSI 时钟源，1Hz 计数，用于定时唤醒）
  */
void LowPower_Init(void)
{
    /* RTC 闹钟唤醒：EXTI17 + NVIC（统一放在低功耗模块里） */
    EXTI_InitTypeDef EXTI_InitStructure;
    EXTI_InitStructure.EXTI_Line    = EXTI_Line17;
    EXTI_InitStructure.EXTI_Mode    = EXTI_Mode_Interrupt;
    EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Rising;
    EXTI_InitStructure.EXTI_LineCmd = ENABLE;
    EXTI_Init(&EXTI_InitStructure);

    NVIC_InitTypeDef NVIC_InitStructure;
    NVIC_InitStructure.NVIC_IRQChannel = RTCAlarm_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;
    NVIC_Init(&NVIC_InitStructure);

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR | RCC_APB1Periph_BKP, ENABLE);
    PWR_BackupAccessCmd(ENABLE);

    /* LSI 不在备份域，每次复位都要重新打开 */
    RCC_LSICmd(ENABLE);
    uint32_t timeout = 100000;
    while (RCC_GetFlagStatus(RCC_FLAG_LSIRDY) == RESET && timeout) timeout--;
    if (RCC_GetFlagStatus(RCC_FLAG_LSIRDY) == RESET)
    {
        Serial3_SendString("LSI Clock Timeout!\r\n");
        return;
    }

    if (BKP_ReadBackupRegister(BKP_DR1) != 0xA5A5)
    {
        RCC_RTCCLKConfig(RCC_RTCCLKSource_LSI);
        RCC_RTCCLKCmd(ENABLE);

        RTC_ClearFlag(RTC_FLAG_RSF);
        if (!RTC_WaitSync())
        {
            Serial3_SendString("RTC Sync Timeout!\r\n");
            return;
        }
        RTC_WaitForLastTask();

        RTC_SetPrescaler(40000 - 1);   /* LSI 约 40kHz -> 1Hz */
        RTC_WaitForLastTask();

        BKP_WriteBackupRegister(BKP_DR1, 0xA5A5);
    }
    else
    {
        RCC_RTCCLKConfig(RCC_RTCCLKSource_LSI);
        RCC_RTCCLKCmd(ENABLE);

        RTC_ClearFlag(RTC_FLAG_RSF);
        if (!RTC_WaitSync())
        {
            Serial3_SendString("RTC Sync Timeout!\r\n");
            return;
        }
    }

    rtc_ready = 1;
}

/**
  * @brief  获取系统运行秒数（RTC 计数，STOP 休眠期间也在走）
  */
uint32_t LowPower_GetSeconds(void)
{
    if (rtc_ready && !RTC_WaitSync()) rtc_ready = 0;
    return rtc_ready ? RTC_GetCounter() : (millis() / 1000);
}

/**
  * @brief  进入 STOP 模式休眠
  */
void LowPower_Sleep(uint32_t ms)
{
    uint32_t wakeup_sec = ms / 1000;
    if (wakeup_sec < 2) wakeup_sec = 2;

    if (!rtc_ready)
    {
        Delay_ms(ms);
        return;
    }

    if (!RTC_WaitSync())
    {
        rtc_ready = 0;
        Delay_ms(ms);
        return;
    }

    /* 等串口最后一个字节移出去 */
    uint32_t tc_timeout = 100000;
    while (USART_GetFlagStatus(USART3, USART_FLAG_TC) == RESET && tc_timeout) tc_timeout--;

    RTC_WaitForLastTask();
    RTC_ClearFlag(RTC_FLAG_ALR);
    RTC_SetAlarm(RTC_GetCounter() + wakeup_sec);
    RTC_WaitForLastTask();

    RTC_ITConfig(RTC_IT_ALR, ENABLE);
    RTC_WaitForLastTask();

    EXTI_ClearITPendingBit(EXTI_Line17);

    SysTick->CTRL &= ~SysTick_CTRL_TICKINT_Msk;

    PWR_EnterSTOPMode(PWR_Regulator_LowPower, PWR_STOPEntry_WFI);

    /* 唤醒后恢复系统时钟 */
    SystemInit();
    SysTick->CTRL |= SysTick_CTRL_TICKINT_Msk;

    /* APB1 时钟停过，RTC 影子寄存器过期，先清 RSF 让下次读取时重新同步 */
    RTC_ClearFlag(RTC_FLAG_RSF);

    Delay_ms(10);
}

/**
  * @brief  进入 Sleep 模式（WFI），时钟不停，串口可唤醒
  */
void LowPower_SleepLight(uint32_t ms)
{
    uint32_t wakeup_sec = ms / 1000;
    if (wakeup_sec < 2) wakeup_sec = 2;

    if (!rtc_ready)
    {
        Delay_ms(ms);
        return;
    }

    if (!RTC_WaitSync())
    {
        rtc_ready = 0;
        Delay_ms(ms);
        return;
    }

    uint32_t tc_timeout = 100000;
    while (USART_GetFlagStatus(USART3, USART_FLAG_TC) == RESET && tc_timeout) tc_timeout--;

    RTC_WaitForLastTask();
    RTC_ClearFlag(RTC_FLAG_ALR);
    RTC_SetAlarm(RTC_GetCounter() + wakeup_sec);
    RTC_WaitForLastTask();

    RTC_ITConfig(RTC_IT_ALR, ENABLE);
    RTC_WaitForLastTask();

    EXTI_ClearITPendingBit(EXTI_Line17);

    /* 只留串口/EXTI/RTC 闹钟能唤醒 */
    SysTick->CTRL &= ~SysTick_CTRL_TICKINT_Msk;
    TIM_ITConfig(TIM2, TIM_IT_Update, DISABLE);
    TIM_ClearITPendingBit(TIM2, TIM_IT_Update);
    NVIC_ClearPendingIRQ(TIM2_IRQn);

    USART_ClearITPendingBit(USART1, USART_IT_RXNE);
    NVIC_ClearPendingIRQ(USART1_IRQn);

    __WFI();

    TIM_ITConfig(TIM2, TIM_IT_Update, ENABLE);
    SysTick->CTRL |= SysTick_CTRL_TICKINT_Msk;

    Delay_ms(2);
}


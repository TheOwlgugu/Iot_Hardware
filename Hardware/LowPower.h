#ifndef __LOWPOWER_H
#define __LOWPOWER_H

#include "stm32f10x.h"

/* 开机后保持唤醒的时间（毫秒），给调试器/烧录留出连接窗口；发布时设为 0 */
#define LOWPOWER_BOOT_GRACE_MS  5000

/* LSI + RTC，1Hz 计数；之后才能用秒计时和休眠 */
void LowPower_Init(void);

/* 系统运行秒数（RTC 计数，STOP 期间也在走） */
uint32_t LowPower_GetSeconds(void);

/* 深睡：STOP 模式，最省电，但 USART1 收不到数据 */
void LowPower_Sleep(uint32_t ms);

/* 浅睡：WFI，时钟不停，串口中断可唤醒（WiFi 下行用） */
void LowPower_SleepLight(uint32_t ms);

#endif

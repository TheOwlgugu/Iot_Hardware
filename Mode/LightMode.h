#ifndef __LIGHTMODE_H
#define __LIGHTMODE_H

#include "stm32f10x.h"

/* 光照传感器（数字 DO，接 PA1）：初始化 GPIO + EXTI1 双边沿中断 */
void LightMode_Init(void);

/* 读光照 DO：返回 0/1（极性取决于模块，遮光前后看变化即可） */
uint8_t LightMode_Read(void);

/* 是否有待处理的电平变化（EXTI1 中断置位，主循环消费） */
uint8_t LightMode_EdgePending(void);

/* 消费掉电平变化标志 */
void LightMode_ClearEdge(void);

#endif

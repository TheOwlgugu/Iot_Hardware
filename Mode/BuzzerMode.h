#ifndef __BUZZERMODE_H
#define __BUZZERMODE_H

#include "stm32f10x.h"

/*
 * 有源蜂鸣器模块（3 针：GND / I/O / VCC，低电平触发）
 * I/O 接 PA1，与光照/DS18B20 复用
 */
void BuzzerMode_Init(void);
void BuzzerMode_On(void);
void BuzzerMode_Off(void);
void BuzzerMode_Beep(uint16_t ms);

#endif

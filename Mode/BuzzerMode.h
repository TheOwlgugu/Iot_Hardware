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

/*
 * 执行一条下行指令（code 语义由蜂鸣器自己定义）：
 *   0=停  1=响100ms  2=响500ms  3=长响（直到 00）
 * 返回 1=已执行，0=不认识的指令
 */
uint8_t BuzzerMode_Command(uint32_t code);

#endif

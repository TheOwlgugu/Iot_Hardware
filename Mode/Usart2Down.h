#ifndef __USART2DOWN_H
#define __USART2DOWN_H

#include "stm32f10x.h"

/*
 * USART2 下行执行器（PA2=TX, PA3=RX, 9600 8N1）
 * 收到端口 3 的下行后，把 5 位 code 以 ASCII 发出去（暂不加换行）。
 * 执行器协议由执行器设计者配合：9600、5 字节 ASCII。
 */
void    Usart2Down_Init(void);
uint8_t Usart2Down_Command(uint32_t code);

#endif

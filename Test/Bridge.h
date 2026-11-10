#ifndef __BRIDGE_H
#define __BRIDGE_H

#include "stm32f10x.h"

/*
 * 调试桥：USART3(CH340) <-> USART1(RA-08) 直通
 * 用于在没有 USB-TTL 的情况下，用电脑直接和 LoRa 模块对话。
 * 进入前会给 LoRa 上电，退出时恢复 USART1 到 115200 并断电。
 */
void Bridge_Run(uint32_t baud);

#endif

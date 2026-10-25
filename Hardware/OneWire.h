#ifndef __ONEWIRE_H
#define __ONEWIRE_H

#include "stm32f10x.h"

/*
 * 1-Wire 总线：PA1（与光照 DO 复用）
 * 硬件需要 PA1 到 3.3V 的 4.7k 上拉
 * 同一时刻只能有一个设备驱动 PA1
 */
void OneWire_Init(void);

/* 复位并检测存在脉冲：1=有器件，0=没有 */
uint8_t OneWire_Reset(void);

void OneWire_WriteByte(uint8_t byte);
uint8_t OneWire_ReadByte(void);

/* Dallas/Maxim CRC8（DS18B20 暂存器校验） */
uint8_t OneWire_Crc8(const uint8_t *data, uint8_t len);

#endif

#ifndef __DS18B20MODE_H
#define __DS18B20MODE_H

#include "stm32f10x.h"

/* 1-Wire 初始化并检测 DS18B20；1=存在，0=没有 */
uint8_t Ds18b20Mode_Init(void);

/* 启动转换并读温度（℃）；1=成功，0=失败/CRC 错 */
uint8_t Ds18b20Mode_Read(float *temp);

#endif

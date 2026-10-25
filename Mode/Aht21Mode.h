#ifndef __AHT21MODE_H
#define __AHT21MODE_H

#include "stm32f10x.h"

/* AHT21 的 7 位 I2C 地址 */
#define AHT21_I2C_ADDR   0x38

/* 初始化/校准；1=成功，0=无应答 */
uint8_t Aht21Mode_Init(void);

/* 触发一次测量并读取温湿度；1=成功 */
uint8_t Aht21Mode_Read(float *temp, float *humi);

#endif

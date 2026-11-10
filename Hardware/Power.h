#ifndef __POWER_H
#define __POWER_H

#include "stm32f10x.h"

/* 传感器 + OLED 总电源：PB0，P-MOS 高边，低电平导通 */
#define SENSOR_PWR_PORT   GPIOB
#define SENSOR_PWR_PIN    GPIO_Pin_0

/* LoRa 电源：PB1，1M 上拉到 3.3V，默认关断；低电平导通 */
#define LORA_PWR_PORT     GPIOB
#define LORA_PWR_PIN      GPIO_Pin_1

void Power_Rail_Init(void);        /* 只初始化电源开关引脚，默认全部关断 */

void Power_Sensor_ON(void);
void Power_Sensor_OFF(void);
void Power_Lora_ON(void);
void Power_Lora_OFF(void);

#endif

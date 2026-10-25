#ifndef __DATAMODE_H
#define __DATAMODE_H

#include "stm32f10x.h"

// 初始化配置的传感器
uint8_t DataMode_Init(void);

// 当前模式下传感器需要的上电稳定时间（毫秒）
uint16_t DataMode_GetSettleMs(void);

// 当前模式是否有传感器需要持续供电（SCD40）
uint8_t DataMode_NeedsContinuousPower(void);

// 执行一次全量采集，把数据存入 sysData
void DataMode_Read_All(void);

// 把 sysData 里的最新数据打包发送（一次一帧，按顺序轮询；LoRa 用）
uint8_t DataMode_SendNext(void);

// 一次把当前所有待发数据都发完（WiFi 用，返回实际发出的帧数）
uint8_t DataMode_SendPending(void);

// 光照变化事件：EXTI 唤醒后调用，值变化才立刻发送
uint8_t DataMode_LightEvent(void);

#endif

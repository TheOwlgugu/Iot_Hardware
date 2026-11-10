#ifndef __DATAMODE_H
#define __DATAMODE_H

#include "stm32f10x.h"

/* 各端口执行/初始化状态，供 OLED 显示 */
typedef struct
{
    uint8_t data_ok;
    uint8_t i2c_ok;
    uint8_t uart2_ok;
} PortStatus_t;

extern PortStatus_t g_portStatus;

// 初始化配置的传感器
uint8_t DataMode_Init(void);

/* 发送周期（秒），已含各链路最小间隔 */
uint32_t DataMode_GetIntervalSec(void);

/* 一次采集（内部处理传感器上下电） */
void DataMode_Sample(void);

/* 按网络类型发送待发数据 */
void DataMode_SendTick(void);

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

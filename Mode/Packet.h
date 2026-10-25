#ifndef __PACKET_H
#define __PACKET_H

#include "stm32f10x.h"

/* 传感器读数类型码（和线上协议一致） */
typedef enum
{
    READING_LIGHT = 0x00,
    READING_TEMP  = 0x01,
    READING_CO2   = 0x02,
    READING_HUMI  = 0x03,
    READING_PM25  = 0x04
} ReadingType_t;

/* 一条待发送的读数 */
typedef struct
{
    uint8_t type;     /* ReadingType_t */
    int32_t value;    /* 已按类型缩放好的值：温度/湿度 x100，CO2/PM2.5 原值 */
    uint8_t valid;    /* 1=有效 */
} Reading_t;

/*
 * 把一条读数编码成 10 位 ASCII：
 *   设备ID(3) + 类型(2) + 数据(5) [+ '\n']
 * newline=1 时追加 '\n'（WiFi），newline=0 时不追加（LoRa）
 * 返回 1=成功，0=参数/范围错误
 */
uint8_t Packet_Build(const Reading_t *reading,
                     const char *device_id,
                     char *buf,
                     uint16_t size,
                     uint8_t newline);

#endif

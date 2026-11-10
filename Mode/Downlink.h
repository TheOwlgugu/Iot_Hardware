#ifndef __DOWNLINK_H
#define __DOWNLINK_H

#include "stm32f10x.h"

/*
 * 下行控制：只负责“收帧 + 按端口转发”，不解释任何执行器语义。
 * 具体指令含义由执行器驱动解释（当前是 BuzzerMode_Command）。
 *
 * 设备帧：10 个 ASCII 字符，和上行等长：
 *   设备号(3) + 端口(1) + 指令(5) + 校验(1)
 *   例如 "002100001S" = 设备 002、端口 1、指令 00001、校验位 S
 *   port 1 = DATA 口（蜂鸣器/光照/DS18B20 所在口）
 *   port 2 = I2C 口
 *   port 3 = USART2 口
 *   port 4 = 测试口（中间层回 ACK 用）
 *
 * WiFi：主循环调 Downlink_Poll() 解析 ESP8266 的 "+IPD,<len>:<data>"
 * LoRa：第二步由 RA-08 驱动解析 "OK+RECV" 后调用 Downlink_HandleFrame()
 */
void Downlink_Init(void);
void Downlink_Poll(void);

/* 按字节喂入下行解析器（在等待 AT 响应时也要调用，避免 +IPD 被吞） */
void Downlink_FeedByte(uint8_t c);
uint8_t Downlink_HandleFrame(const char *data, uint16_t len);

/* 测试模式：取出 port4 的 ACK 序号（取一次就清标志） */
uint8_t Downlink_TakeTestAck(uint32_t *seq);

#endif

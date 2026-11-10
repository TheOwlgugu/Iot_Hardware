#ifndef __TESTMODE_H
#define __TESTMODE_H

#include "stm32f10x.h"

/*
 * 系统测试入口：解析 "test:" 指令并进入对应测试循环。
 * 返回 0 = 未识别（留在配置模式）；进入测试后不返回。
 *   test:net=0 / 1      网络测试（0=WiFi, 1=LoRa）
 *   test:bridge=9600    串口桥接
 */
uint8_t TestMode_Choose(const char *cmd);

#endif

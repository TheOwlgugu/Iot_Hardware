#ifndef __DISPLAY_H
#define __DISPLAY_H

#include "stm32f10x.h"

/*
 * OLED 与传感器共用 PB0 电源轨。工作模式要显示状态，就必须保留供电。
 * 1 = 保留（默认，显示可见，但传感器在休眠期间也带电）
 * 0 = 初始化后断电（省电，但 OLED 会黑屏）
 */
#define DISPLAY_KEEP_POWER  0

/*
 * OLED 结构化显示（4 行 × 16 字符）
 *   Boot    : 设备号 / 模式 / 休眠间隔 / 心跳
 *   Runtime : DATA / I2C / UART2 / NET 四路状态
 */

/* 开机信息：显示 ID、Mode、Sleep、HB；配置 commit/exit 后也调用它刷新 */
void Display_Boot(void);

/* 工作模式：四路状态（正常 OK，失败 NO，未配置 OFF） */
void Display_Runtime(void);

/* 联网引导：第一行网络类型，第二/三行显示进度
 *   WiFi: line1 "NET:WiFi", line2 "WiFi OK/ERR", line3 "IP:x.x.x.x" 或 "IP ERR"
 *   LoRa: line1 "NET:LoRa", line2 "JOIN OK/ERR"
 */
/* 屏幕供电状态：断电后置 0，后续写操作直接跳过 */
void Display_SetPower(uint8_t on);

void Display_NetBegin(uint8_t is_lora);

/* 测试模式：四行固定布局（NET TEST / SEQ / ACK / 状态） */
void Display_TestBegin(uint8_t is_lora);
void Display_TestLine(uint8_t line, const char *text);
void Display_NetLine(uint8_t line, const char *text);

#endif

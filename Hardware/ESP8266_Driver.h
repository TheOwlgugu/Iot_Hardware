#ifndef __ESP8266_DRIVER_H
#define __ESP8266_DRIVER_H

#include "stm32f10x.h"
#include <stdint.h>

uint8_t ESP8266_SendCmd(const char *cmd, const char *expected_response, uint32_t timeout_ms);

/* 解卡：把可能卡在 CIPSEND/透传数据模式的 ESP 拉回命令模式 */
void ESP8266_Recover(void);
uint8_t ESP8266_ConnectWiFi(const char *ssid, const char *pwd);
uint8_t ESP8266_GetIP(char *ip_buf);
uint8_t ESP8266_ConnectServer(const char *ip, uint16_t port);
uint8_t ESP8266_SendData(const char *data);

/* 发送数据后等待某个字符串（用于 AUTH 握手） */
uint8_t ESP8266_SendDataWaitFor(const char *data, const char *expect, uint32_t timeout_ms);

/* last response buffer (NUL terminated), for diagnostics */
const char *ESP8266_LastResponse(void);

/* 提取被 WaitForResponse 吞进缓冲的 +IPD 数据（测试模式用） */
uint8_t ESP8266_TakeIpd(char *out, uint16_t out_size, uint16_t *out_len);

#endif

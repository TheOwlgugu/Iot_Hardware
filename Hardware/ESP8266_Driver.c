#include "ESP8266_Driver.h"
#include "Watchdog.h"
#include "Serial.h"
#include "Delay.h"
#include <string.h>
#include <stdlib.h>
#include "MySPI.h"
#include "W25Q64.h"
#include "Config.h"
#include "Serial3.h"

#define RX_BUF_SIZE  256

static char rx_buf[RX_BUF_SIZE];
static uint16_t rx_index = 0;
static uint16_t ipd_scan = 0;

static void ClearRxBuffer(void)
{
    uint16_t i;

    /* 先清 USART1 环形缓冲，避免上一次/主动上报的数据混进本次响应 */
    for (i = 0; i < 256 && Serial_Available(); i++)
        (void)Serial_GetByte();

    memset(rx_buf, 0, RX_BUF_SIZE);
    rx_index = 0;
    ipd_scan = 0;
}

static uint8_t WaitForResponse(const char *expected, uint32_t timeout_ms)
{
    uint32_t cnt = 0;

    while (cnt < timeout_ms)
    {
        Watchdog_Feed();
        uint16_t max_bytes = 256;      /* prevent infinite loop */

        while (Serial_Available() && max_bytes > 0)
        {
            uint8_t byte = Serial_GetByte();
            if (rx_index < RX_BUF_SIZE - 1)
            {
                rx_buf[rx_index++] = byte;
                rx_buf[rx_index] = '\0';
            }
            max_bytes--;
        }

        if (expected != NULL && strstr(rx_buf, expected) != NULL)
            return 1;

        Delay_ms(1);
        cnt++;
    }
    return 0;
}

/* 从 WaitForResponse 吞进来的 rx_buf 里提取 "+IPD,<len>:<payload>"，
   测试模式下发送 ACK 可能正好落在这个窗口里 */
uint8_t ESP8266_TakeIpd(char *out, uint16_t out_size, uint16_t *out_len)
{
    char *p;
    char *q;
    uint16_t len = 0;
    uint16_t avail;
    uint16_t pos;

    if (out == 0 || out_size == 0) return 0;
    if (ipd_scan >= rx_index) return 0;

    p = strstr(rx_buf + ipd_scan, "+IPD,");
    if (p == 0)
    {
        ipd_scan = rx_index;
        return 0;
    }

    q = p + 5;
    while (*q >= '0' && *q <= '9') { len = (uint16_t)(len * 10 + (*q - '0')); q++; }
    if (*q != ':') { ipd_scan = (uint16_t)(q - rx_buf); return 0; }
    q++;

    pos   = (uint16_t)(q - rx_buf);
    avail = (uint16_t)(rx_index - pos);
    if (len > avail) return 0;              /* 还没收全，下次再试 */

    if (len > out_size - 1) len = out_size - 1;
    memcpy(out, q, len);
    out[len] = '\0';
    if (out_len) *out_len = len;

    ipd_scan = (uint16_t)(pos + len);
    return 1;
}

const char *ESP8266_LastResponse(void)
{
    return rx_buf;
}

void ESP8266_Recover(void)
{
    /* ESP 常供电：STM32 复位后它可能还卡在 CIPSEND 的 N 字节窗口，
       此时 AT 会被当成数据。先灌一段数据填满窗口，再发 +++ 退出数据模式。
       命令模式下这些只是无效 AT 行，没有副作用。 */
    Delay_ms(1000);
    Serial_SendString("AT\r\nAT\r\nAT\r\nAT\r\nAT\r\nAT\r\n");
    Delay_ms(300);
    Serial_SendString("+++");
    Delay_ms(1200);

    /* 回到命令模式后再复位一次，清掉残留的 TCP/透传状态 */
    Serial_SendString("AT+RST\r\n");
    Delay_ms(2500);
    Serial3_SendString("[ESP] recover done\r\n");
}

uint8_t ESP8266_SendCmd(const char *cmd, const char *expected_response, uint32_t timeout_ms)
{
    ClearRxBuffer();
    Serial_SendString((char*)cmd);
    uint8_t ret = WaitForResponse(expected_response, timeout_ms);
    Delay_ms(500);   // ★ 新增：每条指令后延时
    return ret;
}

uint8_t ESP8266_ConnectWiFi(const char *ssid, const char *pwd)
{
    if (!ESP8266_SendCmd("AT+CWMODE=1\r\n", "OK", 2000))
        return 0;

    // ★ 开启 WiFi 自动重连
    ESP8266_SendCmd("AT+CWAUTOCONN=1\r\n", "OK", 2000);

    ESP8266_SendCmd("AT+CWQAP\r\n", "OK", 2000);

    char cmd[128];
    sprintf(cmd, "AT+CWJAP=\"%s\",\"%s\"\r\n", ssid, pwd);

    if (ESP8266_SendCmd(cmd, "WIFI GOT IP", 15000))
        return 1;

    return 0;
}

uint8_t ESP8266_GetIP(char *ip_buf)
{
    ClearRxBuffer();
    Serial_SendString("AT+CIFSR\r\n");
    if (WaitForResponse("+CIFSR:STAIP,\"", 3000))
    {
        char *start = strstr(rx_buf, "+CIFSR:STAIP,\"");
        if (start)
        {
            start += strlen("+CIFSR:STAIP,\"");
            char *end = strchr(start, '\"');
            if (end)
            {
                int len = end - start;
                if (len < 16)
                {
                    memcpy(ip_buf, start, len);
                    ip_buf[len] = '\0';
                    return 1;
                }
            }
        }
    }
    return 0;
}

uint8_t ESP8266_ConnectServer(const char *ip, uint16_t port)
{
    char cmd[64];
    sprintf(cmd, "AT+CIPSTART=\"TCP\",\"%s\",%d\r\n", ip, port);
    /* close stale TCP before CIPSTART */
    ESP8266_SendCmd("AT+CIPCLOSE\r\n", "OK", 1000);
    Delay_ms(200);

    // ★ 新增：重试 3 次
    for (uint8_t i = 0; i < 1; i++)
    {
        if (ESP8266_SendCmd(cmd, "CONNECT", 8000))
            return 1;
        Delay_ms(1000);
    }
    return 0;
}

uint8_t ESP8266_SendDataWaitFor(const char *data, const char *expect, uint32_t timeout_ms)
{
    uint16_t len = strlen(data);
    char cmd[24];

    sprintf(cmd, "AT+CIPSEND=%d\r\n", len);

    ClearRxBuffer();
    Serial_SendString(cmd);

    if (!WaitForResponse(">", 3000)) return 0;
    Serial_SendString((char*)data);

    uint32_t cnt = 0;
    while (cnt < timeout_ms)
    {
        Watchdog_Feed();
        while (Serial_Available())
        {
            uint8_t c = Serial_GetByte();
            if (rx_index < RX_BUF_SIZE - 1)
            {
                rx_buf[rx_index++] = c;
                rx_buf[rx_index] = 0;
            }
        }

        if (strstr(rx_buf, expect) != NULL) return 1;

        Delay_ms(1);
        cnt++;
    }

    return 0;
}

uint8_t ESP8266_SendData(const char *data)
{
    uint16_t len = strlen(data);
    char cmd[24];
    sprintf(cmd, "AT+CIPSEND=%d\r\n", len);

    Serial3_Printf("[CIPSEND] TX: %s", cmd);

    ClearRxBuffer();
    Serial_SendString(cmd);

    if (!WaitForResponse(">", 3000))
    {
        Serial3_Printf("[CIPSEND] Timeout waiting '>', RX: %s\r\n", rx_buf);
        return 0;
    }
    Serial3_SendString("[CIPSEND] Got '>'\r\n");

    Serial_SendString((char*)data);

    if (!WaitForResponse("SEND OK", 5000))
    {
        Serial3_Printf("[CIPSEND] Timeout waiting SEND OK, RX: %s\r\n", rx_buf);
        return 0;
    }
    Serial3_SendString("[CIPSEND] SEND OK\r\n");

    return 1;
}

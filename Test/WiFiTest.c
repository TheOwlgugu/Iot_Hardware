#include "WiFiTest.h"
#include "Serial.h"
#include "Serial3.h"
#include "Delay.h"
#include "ESP8266_Driver.h"
#include "Downlink.h"
#include "Config.h"
#include <string.h>
#include "Network.h"
#include "Power.h"
#include <stdio.h>

static uint32_t s_baud = 115200;

/**
  * @brief  唤醒 ESP 并探测波特率
  * @note   若上一次复位时 ESP 正卡在 CIPSEND 的数据窗口，AT 会被当成数据吃掉；
  *         先等 1 秒再发 "+++" 退出数据模式（命令模式下它只是回 ERROR）
  */
static uint8_t KickAndProbe(void)
{
    static const uint32_t bauds[] = {115200, 74880, 9600, 57600, 38400, 230400};
    uint8_t i;

    ESP8266_Recover();   /* 解卡：填 CIPSEND 窗口 + 退出数据模式 */

    for (i = 0; i < sizeof(bauds) / sizeof(bauds[0]); i++)
    {
        Serial_SetBaudRate(bauds[i]);
        Delay_ms(100);
        if (ESP8266_SendCmd("AT\r\n", "OK", 1000))
        {
            s_baud = bauds[i];
            Serial3_Printf("[TEST] ESP AT OK at %lu\r\n", (unsigned long)s_baud);
            return 1;
        }
    }

    Serial3_SendString("[TEST] ESP no AT\r\n");
    return 0;
}

static uint8_t WiFiTest_Init(void)
{
    g_connType = NET_WIFI;             /* Downlink_Poll 用它判断 */
    Power_Lora_OFF();                  /* 避免 RA-08 同时挂在 USART1 上 */
    Serial_SetBaudRate(115200);
    Delay_ms(100);
    return KickAndProbe();
}

static uint8_t WiFiTest_Connect(void)
{
    Serial_SetBaudRate(s_baud);
    Delay_ms(100);

    if (!ESP8266_ConnectWiFi(config.wifi_ssid, config.wifi_pwd))
    {
        Serial3_SendString("[TEST] WiFi join FAIL\r\n");
        return 0;
    }
    Serial3_SendString("[TEST] WiFi join OK\r\n");

    Delay_ms(300);
    if (!ESP8266_ConnectServer(config.wifi_ip, config.wifi_port))
    {
        Serial3_SendString("[TEST] TCP FAIL\r\n");
        return 0;
    }
    Serial3_SendString("[TEST] TCP OK\r\n");

    if (config_token[0] != '\0')
    {
        char auth[64];
        snprintf(auth, sizeof(auth), "AUTH,%s,%s\n", config.device_id, config_token);
        if (!ESP8266_SendDataWaitFor(auth, "AUTH,OK", 5000))
        {
            Serial3_SendString("[TEST] AUTH FAIL\r\n");
            return 0;
        }
        Serial3_SendString("[TEST] AUTH OK\r\n");
    }

    return 1;
}

/* 等待一个字符，期间把收到的所有字节喂给下行解析器（+IPD 不会丢） */
static uint8_t WaitForChar(uint8_t want, uint32_t ms)
{
    while (ms--)
    {
        while (Serial_Available())
        {
            uint8_t c = Serial_GetByte();
            Downlink_FeedByte(c);
            if (c == want) return 1;
        }
        Delay_ms(1);
    }
    return 0;
}

/* 测试模式专用发送：只等 '>'，不等 SEND OK；
   SEND OK 和 +IPD 混在一起，统一交给字节解析器 */
static uint8_t WiFiTest_Send(const char *frame)
{
    char cmd[24];

    sprintf(cmd, "AT+CIPSEND=%d\r\n", (int)strlen(frame));
    Serial_SendString(cmd);

    if (!WaitForChar('>', 3000)) return 0;

    Serial_SendString((char*)frame);
    return 1;
}

static void WiFiTest_Poll(void)
{
    Downlink_Poll();

    /* 发送时 WaitForResponse 可能把 +IPD 吞进 rx_buf，这里补扫 */
    {
        char     ipd[32];
        uint16_t ipd_len;
        while (ESP8266_TakeIpd(ipd, sizeof(ipd), &ipd_len))
            Downlink_HandleFrame(ipd, ipd_len);
    }
}

static const NetTestOps s_ops = {
    "WiFi", 0,
    WiFiTest_Init, WiFiTest_Connect, WiFiTest_Send, WiFiTest_Poll
};

const NetTestOps *WiFiTest_Ops(void) { return &s_ops; }

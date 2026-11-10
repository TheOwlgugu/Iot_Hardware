#include "WiFiMode.h"
#include "Display.h"
#include <stdio.h>
#include "Serial.h"
#include "Serial3.h"
#include "Delay.h"
#include "OLED.h"
#include "ESP8266_Driver.h"
#include "Config.h"

/* 波特率探测只做一次：断网重连时不再重复扫 6 个波特率（一次要 8 秒左右） */
static uint32_t baud_known  = 0;

/**
  * @brief  第一次 AT 失败时扫一遍常见波特率，结果记住
  * @retval 找到的波特率；0=一个都没通
  */
static uint32_t ProbeBaud(void)
{
    static const uint32_t bauds[] = {115200, 74880, 9600, 57600, 38400, 230400};
    uint8_t i;

    Serial3_SendString("AT fail, probing baud (once)...\r\n");

    for (i = 0; i < sizeof(bauds) / sizeof(bauds[0]); i++)
    {
        Serial_SetBaudRate(bauds[i]);
        Delay_ms(50);
        if (ESP8266_SendCmd("AT\r\n", "OK", 800))
            return bauds[i];
    }

    return 0;
}

/**
  * @brief  打印最后一次收到的原始字节（只在首次探测全失败时调用）
  */
static void DumpLastRx(void)
{
    const char *rx = ESP8266_LastResponse();
    uint16_t n = 0;

    Serial3_SendString("RX hex:");
    while (rx[n] != '\0' && n < 32)
    {
        char one[4];
        one[0] = "0123456789ABCDEF"[((uint8_t)rx[n]) >> 4];
        one[1] = "0123456789ABCDEF"[((uint8_t)rx[n]) & 0x0F];
        one[2] = ' ';
        one[3] = '\0';
        Serial3_SendString(one);
        n++;
    }
    Serial3_SendString("\r\n");
}

/**
  * @brief  WiFi 连接全流程
  * @note   包含：AT测试 → 连接WiFi → 连接TCP服务器
  */
uint8_t WifiConnect(void)
{
    uint8_t at_ok;

    Serial3_SendString("=== WiFi Connect ===\r\n");
    Display_NetBegin(0);

    Serial_SetBaudRate(baud_known ? baud_known : 115200);
    Delay_ms(100);

    /* 1. 先直接测 AT：正常重连不打扰 ESP */
    at_ok = ESP8266_SendCmd("AT\r\n", "OK", 1500);

    /* 只有 AT 失败才解卡（可能卡在 CIPSEND/透传） */
    if (!at_ok)
    {
        ESP8266_Recover();
        Delay_ms(100);
        at_ok = ESP8266_SendCmd("AT\r\n", "OK", 2000);
    }

    if (!at_ok)
    {
        /* 每次都重新探测，避免一次失败后把后续重试全卡死 */
        baud_known = ProbeBaud();

        if (baud_known)
        {
            Serial3_Printf("ESP8266 AT OK at %u baud\r\n", (unsigned int)baud_known);
            at_ok = 1;
        }
        else
        {
            Serial3_SendString("No AT on any baud. Check 3V3, TX/RX, CH_PD.\r\n");
            DumpLastRx();
            Serial_SetBaudRate(115200);
        }
    }
    else if (!baud_known)
    {
        baud_known = 115200;
    }

    if (!at_ok)
    {
        Display_NetLine(2, "WiFi ERR");
        Serial3_SendString("AT fail\r\n");
        return 0;
    }

    /* 2. 连接 WiFi */
    Display_NetLine(2, "WiFi...");
    if (!ESP8266_ConnectWiFi(config.wifi_ssid, config.wifi_pwd))
    {
        Display_NetLine(2, "WiFi ERR");
        Serial3_SendString("WiFi fail\r\n");
        return 0;
    }
    Display_NetLine(2, "WiFi OK");
    Serial3_SendString("WiFi OK\r\n");

    Delay_ms(500);

    /* 3. 连接 TCP 服务器 */
    Display_NetLine(3, "TCP...");
    if (!ESP8266_ConnectServer(config.wifi_ip, config.wifi_port))
    {
        Display_NetLine(3, "IP ERR");
        Serial3_SendString("TCP fail\r\n");
        return 0;
    }
    Serial3_SendString("TCP OK\r\n");

    /* 连接认证：服务器只接受带 token 的设备 */
    if (config_token[0] != '\0')
    {
        char auth[64];
        snprintf(auth, sizeof(auth), "AUTH,%s,%s\n", config.device_id, config_token);
        if (!ESP8266_SendDataWaitFor(auth, "AUTH,OK", 5000))
        {
            Display_NetLine(3, "AUTH ERR");
            Serial3_SendString("AUTH fail\r\n");
            return 0;
        }
        Serial3_SendString("AUTH OK\r\n");
    }

    {
        char ip[16];
        char line[17];

        if (ESP8266_GetIP(ip))
        {
            snprintf(line, sizeof(line), "IP:%s", ip);
            Display_NetLine(3, line);
            Serial3_Printf("STA IP: %s\r\n", ip);
        }
        else
        {
            Display_NetLine(3, "IP ERR");
        }
    }

    return 1;
}

/**
  * @brief  WiFi 发送数据
  * @note   只负责发送。失败后的重连由 Network 层的状态机统一处理，
  *         避免这里重连一次、上层再重连一次，把主循环卡住。
  */
uint8_t WifiSend(const char *msg)
{
    return ESP8266_SendData(msg);
}

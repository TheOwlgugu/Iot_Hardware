#include "WiFiMode.h"
#include "Serial.h"
#include "Serial3.h"
#include "Delay.h"
#include "OLED.h"
#include "ESP8266_Driver.h"
#include "Config.h"

/* 波特率探测只做一次：断网重连时不再重复扫 6 个波特率（一次要 8 秒左右） */
static uint8_t  baud_probed = 0;
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

    /* 已知波特率就直接用，避免每次重连都从 115200 白试一次 */
    Serial_SetBaudRate(baud_known ? baud_known : 115200);
    Delay_ms(100);

    /* 1. 测 AT */
    OLED_ShowString(1, 1, "AT:");
    at_ok = ESP8266_SendCmd("AT\r\n", "OK", 3000);

    if (!at_ok && !baud_probed)
    {
        /* 首次失败才扫波特率，结果记住，后续重连不再扫 */
        baud_probed = 1;
        baud_known  = ProbeBaud();

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
    else if (!at_ok && baud_known)
    {
        /* 已知波特率但这次没应答：在正确波特率上再试一次 */
        Serial_SetBaudRate(baud_known);
        at_ok = ESP8266_SendCmd("AT\r\n", "OK", 1500);
    }

    if (!at_ok)
    {
        OLED_ShowString(1, 6, "FAIL");
        Serial3_SendString("AT fail\r\n");
        return 0;
    }
    OLED_ShowString(1, 6, "OK  ");

    /* 2. 连接 WiFi */
    OLED_ShowString(2, 1, "Wifi:");
    if (!ESP8266_ConnectWiFi(config.wifi_ssid, config.wifi_pwd))
    {
        OLED_ShowString(2, 6, "FAIL");
        Serial3_SendString("WiFi fail\r\n");
        return 0;
    }
    OLED_ShowString(2, 6, "OK  ");
    Serial3_SendString("WiFi OK\r\n");

    Delay_ms(500);

    /* 3. 连接 TCP 服务器 */
    OLED_ShowString(3, 1, "TCP:");
    if (!ESP8266_ConnectServer(config.wifi_ip, config.wifi_port))
    {
        OLED_ShowString(3, 6, "FAIL");
        Serial3_SendString("TCP fail\r\n");
        return 0;
    }
    OLED_ShowString(3, 6, "OK  ");
    Serial3_SendString("TCP OK\r\n");

    {
        char ip[16];
        if (ESP8266_GetIP(ip))
            Serial3_Printf("STA IP: %s\r\n", ip);
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

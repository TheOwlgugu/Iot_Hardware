#include "Display.h"
#include "OLED.h"
#include "Config.h"
#include "NodeMode.h"
#include "DataMode.h"
#include "Network.h"
#include <stdio.h>
#include <string.h>

static uint8_t display_on = 1;

void Display_SetPower(uint8_t on)
{
    display_on = on;
}

/* 写一整行：先铺 16 个空格，避免旧字符残留 */
static void ShowLine(uint8_t line, const char *text)
{
    char buf[17];

    if (!display_on) return;

    uint8_t i;

    for (i = 0; i < 16; i++) buf[i] = ' ';
    buf[16] = '\0';

    if (text != 0)
    {
        for (i = 0; text[i] != '\0' && i < 16; i++) buf[i] = text[i];
    }

    OLED_ShowString(line, 1, buf);
}

void Display_Boot(void)
{
    char line[17];
    uint8_t has_reading = 0;

    snprintf(line, sizeof(line), "ID:%s", config.device_id);
    ShowLine(1, line);

    snprintf(line, sizeof(line), "Mode:%s", config.mode);
    ShowLine(2, line);

    snprintf(line, sizeof(line), "Sleep:%lus", (unsigned long)(config.send_interval_ms / 1000));
    ShowLine(3, line);

    /* 有读数的节点不发心跳；纯执行器节点每 NETWORK_HEARTBEAT_SEC 秒一次 */
    if (strlen(config.mode) == 4)
    {
        if (config.mode[0] == '1' || config.mode[0] == '3') has_reading = 1;
        if (config.mode[1] != '0') has_reading = 1;
        if (config.mode[2] != '0') has_reading = 1;
    }

    if (has_reading)
        snprintf(line, sizeof(line), "HB:--");
    else
        snprintf(line, sizeof(line), "HB:%us", (unsigned int)NETWORK_HEARTBEAT_SEC);
    ShowLine(4, line);
}

void Display_Runtime(void)
{
    char line[17];

    switch (g_nodeMode.data)
    {
    case DATA_PORT_LIGHT:
        snprintf(line, sizeof(line), "Light %s", g_portStatus.data_ok ? "OK" : "NO");
        break;
    case DATA_PORT_BUZZER:
        snprintf(line, sizeof(line), "Buzzer %s", g_portStatus.data_ok ? "OK" : "NO");
        break;
    case DATA_PORT_DS18B20:
        snprintf(line, sizeof(line), "DS18B20 %s", g_portStatus.data_ok ? "OK" : "NO");
        break;
    default:
        snprintf(line, sizeof(line), "DATA OFF");
        break;
    }
    ShowLine(1, line);

    switch (g_nodeMode.i2c)
    {
    case I2C_PORT_AHT21:
        snprintf(line, sizeof(line), "AHT21 %s", g_portStatus.i2c_ok ? "OK" : "NO");
        break;
    case I2C_PORT_SCD40:
        snprintf(line, sizeof(line), "SCD40 %s", g_portStatus.i2c_ok ? "OK" : "NO");
        break;
    default:
        snprintf(line, sizeof(line), "I2C OFF");
        break;
    }
    ShowLine(2, line);

    switch (g_nodeMode.uart2)
    {
    case UART2_PORT_PMS7003:
        snprintf(line, sizeof(line), "PMS7003 %s", g_portStatus.uart2_ok ? "OK" : "NO");
        break;
    default:
        snprintf(line, sizeof(line), "U2 OFF");
        break;
    }
    ShowLine(3, line);

    snprintf(line, sizeof(line), "%s %s", NodeMode_NetName(g_nodeMode.net), Network_IsUp() ? "OK" : "--");
    ShowLine(4, line);
}

void Display_NetBegin(uint8_t is_lora)
{
    ShowLine(1, is_lora ? "NET:LoRa" : "NET:WiFi");
    ShowLine(2, "connecting...");
    ShowLine(3, "");
    ShowLine(4, "");
}

void Display_NetLine(uint8_t line, const char *text)
{
    if (line < 1 || line > 4) return;
    ShowLine(line, text);
}

void Display_TestBegin(uint8_t is_lora)
{
    display_on = 1;
    ShowLine(1, is_lora ? "NET TEST LoRa" : "NET TEST WiFi");
    ShowLine(2, "SEQ:00000");
    ShowLine(3, "ACK:--");
    ShowLine(4, "init");
}

void Display_TestLine(uint8_t line, const char *text)
{
    if (line < 1 || line > 4) return;
    ShowLine(line, text);
}

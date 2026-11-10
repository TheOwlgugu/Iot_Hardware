#include "LoRaMode.h"
#include "Display.h"
#include "Downlink.h"
#include "Serial.h"
#include "Serial3.h"
#include "Delay.h"
#include "OLED.h"
#include "Ra08_Driver.h"
#include "Config.h"
#include <string.h>
#include <stdio.h>

/**
  * @brief  10位ASCII字符串 → 10字节数组（用于 Ra08_SendData）
  * @param  str  输入的10位字符串
  * @param  out  输出的10字节数组
  */
static void StringToBytes(const char *str, uint8_t *out)
{
    for (uint8_t i = 0; i < 10; i++)
        out[i] = (uint8_t)str[i];
}

/**
  * @brief  LoRa 连接全流程
  * @note   包含：Init → Provision（首次）→ Join
  */
uint8_t LoRaConnect(void)
{
    Serial3_SendString("=== LoRa Connect ===\r\n");
    Display_NetBegin(1);

    /* 1. 初始化 Ra-08（会切换串口波特率到 9600） */
    if (!Ra08_Init())
    {
        Display_NetLine(2, "INIT ERR");
        Serial3_SendString("Ra08 init fail\r\n");
        return 0;
    }

    /* 打印版本/EUI/Class/频组，方便和 ChirpStack 上的配置对照 */
    Ra08_PrintInfo();

    /* 2. 首次烧录时 Provision（写入 DevEUI/AppEUI/AppKey） */
    if (!config.lora_provisioned)
    {
        Serial3_SendString("Provisioning...\r\n");
        if (!Ra08_Provision(config.lora_deveui,
                            config.lora_appeui,
                            config.lora_appkey))
        {
            Serial3_SendString("Provision fail\r\n");
            return 0;
        }

        /* Ra08_Provision 内部已经写过 Class/频组掩码并 CSAVE，
           这里只需要把"已配置"标记存回 STM32，避免重复擦写模块 Flash */

        /* 标记已配置 */
        config.lora_provisioned = 1;
        Config_Save();
        Serial3_SendString("Provision done\r\n");
    }

    /* 3. 入网 */
    Display_NetLine(2, "JOIN...");
    if (!Ra08_Join())
    {
        Display_NetLine(2, "JOIN ERR");
        Serial3_SendString("Join fail\r\n");
        return 0;
    }
    Display_NetLine(2, "JOIN OK");
    Serial3_SendString("Join OK\r\n");

    return 1;
}

/**
  * @brief  LoRa 发送数据
  * @note   字符串 → 字节数组 → Ra08_SendData
  */
/**
  * @brief  取一帧 LoRa 下行并转给统一的下行处理
  * @note   模块在每次上行后的接收窗口把下行存进自己的 buffer，
  *         这里用 AT+DRX? 取，不需要在 RX 窗口一直等着。
  */
static void LoRaPollDownlink(void)
{
    char buf[16];
    uint16_t len = 0;

    if (Ra08_ReadDownlink(buf, sizeof(buf) - 1, &len))
    {
        buf[len] = '\0';
        Serial3_Printf("[LoRa] downlink %u\r\n", (unsigned int)len);
        Downlink_HandleFrame(buf, len);
    }
}
uint8_t LoRaSend(const char *msg)
{
    uint8_t payload[10];

    /* 先看有没有上一轮收到的下行 */
    LoRaPollDownlink();

    // 只要长度小于 10 就不合法；如果是 11（带 \n），也允许过
    if (strlen(msg) < 10)
    {
        Serial3_SendString("LoRa payload too short\r\n");
        return 0;
    }

    // StringToBytes 内部只取前 10 个字节，会自动忽略第 11 个字节 '\n'
    StringToBytes(msg, payload);

    Ra08_SendResult r = Ra08_SendData(payload, 10, 0);   // 0=非确认模式

    return (r == RA08_SEND_OK) ? 1 : 0;
}

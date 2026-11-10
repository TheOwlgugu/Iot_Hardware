#include "LoRaTest.h"
#include "Ra08_Driver.h"
#include "Downlink.h"
#include "Config.h"
#include "Serial3.h"
#include "Delay.h"
#include "Power.h"
#include <string.h>

static uint8_t LoRaTest_Init(void)
{
    /* 测试模式从配置模式进入，LoRa 电源还是关的，必须先上电 */
    Power_Lora_ON();
    Delay_ms(300);

    return Ra08_Init();
}

static uint8_t LoRaTest_Connect(void)
{
    Ra08_PrintInfo();

    if (!config.lora_provisioned)
    {
        Serial3_SendString("Provisioning...\r\n");
        if (!Ra08_Provision(config.lora_deveui, config.lora_appeui, config.lora_appkey))
        {
            Serial3_SendString("Provision fail\r\n");
            return 0;
        }

        config.lora_provisioned = 1;
        Config_Save();
        Serial3_SendString("Provision done\r\n");
    }

    Serial3_SendString("Joining...\r\n");
    if (!Ra08_Join())
    {
        Serial3_SendString("Join fail\r\n");
        return 0;
    }
    Serial3_SendString("Join OK\r\n");

    return 1;
}

static uint8_t LoRaTest_Send(const char *frame)
{
    uint8_t payload[10];
    uint8_t i;

    if (strlen(frame) < 10) return 0;
    for (i = 0; i < 10; i++) payload[i] = (uint8_t)frame[i];

    return (Ra08_SendData(payload, 10, 0) == RA08_SEND_OK) ? 1 : 0;
}

static void LoRaTest_Poll(void)
{
    char     buf[32];
    uint16_t len = 0;

    if (Ra08_ReadDownlink(buf, sizeof(buf), &len))
        Downlink_HandleFrame(buf, len);
}

static const NetTestOps s_ops = {
    "LoRa", 1,
    LoRaTest_Init, LoRaTest_Connect, LoRaTest_Send, LoRaTest_Poll
};

const NetTestOps *LoRaTest_Ops(void) { return &s_ops; }

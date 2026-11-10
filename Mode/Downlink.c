#include "Downlink.h"
#include "Serial.h"
#include "Serial3.h"
#include "NodeMode.h"
#include "BuzzerMode.h"
#include "Config.h"
#include "Network.h"
#include "ESP8266_Driver.h"
#include <string.h>

/* +IPD 解析状态机 */
static uint8_t  ipd_state = 0;
static uint16_t ipd_len   = 0;
static uint8_t  ipd_got   = 0;
static char     ipd_buf[16];

/* 校验码字符表：前 9 个字符之和 % 36，和中间层保持一致 */
static const char CHECK_CHARS[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";

static void Downlink_Execute(uint8_t port, uint32_t code);

/* 测试口 ACK：中间层回 port4 + 原序号 */
static volatile uint32_t test_ack_seq = 0;
static volatile uint8_t  test_ack_flag = 0;

void Downlink_Init(void)
{
    ipd_state = 0;
    ipd_len   = 0;
    ipd_got   = 0;
    test_ack_flag = 0;
}

uint8_t Downlink_TakeTestAck(uint32_t *seq)
{
    if (!test_ack_flag) return 0;
    if (seq) *seq = test_ack_seq;
    test_ack_flag = 0;
    return 1;
}

/**
  * @brief  主循环调用：解析 USART1 上 ESP8266 的 "+IPD,<len>:<data>"
  * @note   非阻塞，一次把所有已到的字节处理完
  */
void Downlink_FeedByte(uint8_t c)
{
    switch (ipd_state)
    {
    case 0:
        if (c == '+') ipd_state = 1;
        break;
    case 1:
        ipd_state = (c == 'I') ? 2 : 0;
        break;
    case 2:
        ipd_state = (c == 'P') ? 3 : ((c == '+') ? 1 : 0);
        break;
    case 3:
        ipd_state = (c == 'D') ? 4 : ((c == '+') ? 1 : 0);
        break;
    case 4:
        ipd_state = (c == ',') ? 5 : ((c == '+') ? 1 : 0);
        break;
    case 5:
        if (c >= '0' && c <= '9') { ipd_len = (uint16_t)(c - '0'); ipd_state = 6; }
        else ipd_state = 0;
        break;
    case 6:
        if (c >= '0' && c <= '9')
        {
            ipd_len = (uint16_t)(ipd_len * 10 + (c - '0'));
            if (ipd_len > 32) ipd_state = 0;
        }
        else if (c == ':')
        {
            ipd_got   = 0;
            ipd_state = (ipd_len > 0 && ipd_len < sizeof(ipd_buf)) ? 7 : 0;
        }
        else ipd_state = 0;
        break;
    case 7:
        ipd_buf[ipd_got++] = (char)c;
        if (ipd_got >= ipd_len)
        {
            ipd_buf[ipd_got] = '\0';
            Downlink_HandleFrame(ipd_buf, ipd_got);
            ipd_state = 0;
        }
        break;
    default:
        ipd_state = 0;
        break;
    }
}

void Downlink_Poll(void)
{
    /* LoRa 模式由 RA-08 驱动用 AT+DRX? 取，不在这里处理 */
    if (g_connType != NET_WIFI) return;

    while (Serial_Available())
        Downlink_FeedByte(Serial_GetByte());

    /* 发送时 WaitForResponse 可能把 +IPD 吞进驱动缓冲，这里补扫 */
    {
        char     ipd[32];
        uint16_t ipd_len;
        while (ESP8266_TakeIpd(ipd, sizeof(ipd), &ipd_len))
            Downlink_HandleFrame(ipd, ipd_len);
    }
}

/**
  * @brief  校验并分发一帧下行
  * @param  data  10 个 ASCII 字符
  * @param  len   实际长度（WiFi 会多一个 '\n'）
  * @retval 1=已交给执行器，0=校验失败/不是本机
  */
uint8_t Downlink_HandleFrame(const char *data, uint16_t len)
{
    uint16_t i;
    uint16_t sum = 0;
    uint8_t  port;
    uint32_t code = 0;

    if (data == 0 || len < 10) return 0;

    /* 1. 校验位 */
    for (i = 0; i < 9; i++) sum += (uint8_t)data[i];
    if (data[9] != CHECK_CHARS[sum % 36])
    {
        Serial3_SendString("[DL] bad checksum\r\n");
        return 0;
    }

    /* 2. 设备号必须是本机 */
    if (strncmp(data, config.device_id, 3) != 0)
    {
        Serial3_SendString("[DL] not for me\r\n");
        return 0;
    }

    /* 3. 端口 1-4（4 = 测试口） */
    if (data[3] < '1' || data[3] > '4') return 0;
    port = (uint8_t)(data[3] - '0');

    /* 4. 5 位指令 */
    for (i = 4; i < 9; i++)
    {
        if (data[i] < '0' || data[i] > '9') return 0;
        code = code * 10 + (uint32_t)(data[i] - '0');
    }

    Downlink_Execute(port, code);
    return 1;
}

/**
  * @brief  按端口分发：本文件只负责转发，指令语义由执行器驱动解释
  */
static void Downlink_Execute(uint8_t port, uint32_t code)
{
    Serial3_Printf("[DL] port=%u code=%lu\r\n", (unsigned int)port, (unsigned long)code);

    switch (port)
    {
    case 1:     /* DATA 口 */
        if (g_nodeMode.data == DATA_PORT_BUZZER)
        {
            if (!BuzzerMode_Command(code))
                Serial3_Printf("[DL] buzzer code %lu unsupported\r\n", (unsigned long)code);
        }
        else
        {
            Serial3_SendString("[DL] port1 no actuator\r\n");
        }
        break;

    case 2:     /* I2C 口：以后接执行器时在这里转发 */
        Serial3_SendString("[DL] port2 (I2C) no actuator\r\n");
        break;

    case 3:     /* UART2 口：以后接执行器时在这里转发 */
        Serial3_SendString("[DL] port3 (UART2) no actuator\r\n");
        break;

    case 4:     /* 测试口：只记录 ACK 序号，由 NetTest 取走 */
        test_ack_seq  = code;
        test_ack_flag = 1;
        break;

    default:
        break;
    }
}

#include "NetTest.h"
#include "stm32f10x.h"
#include "Millis.h"
#include "Delay.h"
#include "Display.h"
#include "Downlink.h"
#include "Packet.h"
#include "Config.h"
#include "Key.h"
#include "Serial3.h"
#include <stdio.h>
#include <string.h>

#define NETTEST_ACK_TIMEOUT_MS   5000
#define NETTEST_LORA_MIN_MS      6000
#define NETTEST_WIFI_GUARD_MS    500
#define NETTEST_AUTO_MS          7000
#define NETTEST_RETRY_MS         5000

enum { ST_IDLE = 0, ST_SENDING, ST_ERROR };

static const NetTestOps *s_ops;
static uint8_t  s_auto;
static uint32_t s_t_auto;

static uint32_t s_next_seq;
static uint32_t s_last_seq;
static uint32_t s_t_send;
static uint32_t s_t_retry;
static uint8_t  s_state;
static uint8_t  s_ack;          /* 0=--, 1=OK, 2=NO */
static char     s_frame[16];

/* PB12/PB14 下降沿 + 20ms 消抖；按住不会重复触发 */
static uint8_t EdgePressed(uint16_t pin, uint8_t *prev)
{
    uint8_t now     = GPIO_ReadInputDataBit(GPIOB, pin);
    uint8_t pressed = 0;

    if (*prev == 1 && now == 0)
    {
        Delay_ms(20);
        if (GPIO_ReadInputDataBit(GPIOB, pin) == 0) pressed = 1;
    }

    *prev = now;
    return pressed;
}

/* 发一帧测试数据（手动按键和自动模式共用） */
static void DoSend(void)
{
    Reading_t r;

    r.type  = READING_TEST;
    r.value = (int32_t)s_next_seq;
    r.valid = 1;

    s_last_seq = s_next_seq;
    s_next_seq = (s_next_seq + 1) % 100000;

    if (Packet_Build(&r, config.device_id, s_frame, sizeof(s_frame), s_ops->is_lora ? 0 : 1) &&
        s_ops->send(s_frame))
    {
        s_state  = ST_SENDING;
        s_ack    = 0;
        s_t_send = millis();
        if (s_auto) s_t_auto = s_t_send;
        Serial3_Printf("[TEST] send %05lu%s\r\n",
                       (unsigned long)s_last_seq, s_auto ? " (auto)" : "");
    }
    else
    {
        s_state   = ST_ERROR;
        s_t_retry = millis();
        Serial3_SendString("[TEST] send fail\r\n");
    }
}

void NetTest_Run(const NetTestOps *ops)
{
    char     line[17];
    char     last1[17] = "";
    char     last2[17] = "";
    char     last3[17] = "";
    char     last4[17] = "";
    uint32_t gate_ms;
    uint8_t  prev12 = 1;
    uint8_t  prev14 = 1;

    s_ops      = ops;
    s_auto     = 0;
    s_t_auto   = 0;
    s_next_seq = 0;
    s_last_seq = 0;
    s_ack      = 0;

    gate_ms = ops->is_lora ? config.send_interval_ms : NETTEST_WIFI_GUARD_MS;
    if (ops->is_lora && gate_ms < NETTEST_LORA_MIN_MS) gate_ms = NETTEST_LORA_MIN_MS;

    Millis_Init();
    Key_Init();
    Display_TestBegin(ops->is_lora);

    /* 测试程序自己的初始化 + 连接，失败进入 error，之后每 5 秒重试 connect */
    if (ops->init() && ops->connect())
    {
        s_state = ST_IDLE;
        Serial3_SendString("[TEST] link up\r\n");
    }
    else
    {
        s_state   = ST_ERROR;
        s_t_retry = millis();
    }

    for (;;)
    {
        uint32_t now = millis();

        /* 下行 */
        ops->poll();

        /* 测试口 ACK：允许落后 1-2 帧（LoRa 的 ACK 可能排到下一帧之后） */
        {
            uint32_t ack_seq;
            if (Downlink_TakeTestAck(&ack_seq))
            {
                uint32_t behind = (s_last_seq - ack_seq + 100000) % 100000;
                if (behind <= 2) s_ack = 1;
                Serial3_Printf("[TEST] ack %05lu (last %05lu, behind %lu)\r\n",
                               (unsigned long)ack_seq, (unsigned long)s_last_seq,
                               (unsigned long)behind);
            }
        }

        /* KEY1（PB12）：切换自动发送 */
        if (EdgePressed(GPIO_Pin_12, &prev12))
        {
            s_auto = s_auto ? 0 : 1;
            s_t_auto = now;
            Serial3_Printf("[TEST] auto %s\r\n", s_auto ? "on" : "off");
        }

        /* KEY2（PB14）：手动发一帧 */
        if (EdgePressed(GPIO_Pin_14, &prev14) && s_state == ST_IDLE)
            DoSend();

        /* 自动模式：每 7 秒等效按一次 KEY2（忙时跳过） */
        if (s_auto && s_state == ST_IDLE && (now - s_t_auto) >= NETTEST_AUTO_MS)
        {
            DoSend();
            s_t_auto = now;
        }

        /* 断线重连（只重试 connect，不重复 init） */
        if (s_state == ST_ERROR && (now - s_t_retry) >= NETTEST_RETRY_MS)
        {
            s_t_retry = now;
            Serial3_SendString("[TEST] retry connect...\r\n");
            if (ops->connect())
            {
                s_state = ST_IDLE;
                Serial3_SendString("[TEST] link up\r\n");
            }
        }

        /* 发送窗口：LoRa 等占空比，WiFi 等 ACK/超时 */
        if (s_state == ST_SENDING)
        {
            uint32_t elapsed = millis() - s_t_send;

            if (s_ack != 1 && elapsed >= NETTEST_ACK_TIMEOUT_MS) s_ack = 2;

            if (ops->is_lora)
            {
                if (elapsed >= gate_ms) s_state = ST_IDLE;
            }
            else
            {
                if (s_ack == 1 || elapsed >= NETTEST_ACK_TIMEOUT_MS) s_state = ST_IDLE;
            }
        }

        /* OLED 只在内容变化时刷新 */
        snprintf(line, sizeof(line), "NET TEST %s", ops->name);
        if (strcmp(line, last1) != 0) { strcpy(last1, line); Display_TestLine(1, line); }

        snprintf(line, sizeof(line), "SEQ:%05lu", (unsigned long)s_last_seq);
        if (strcmp(line, last2) != 0) { strcpy(last2, line); Display_TestLine(2, line); }

        snprintf(line, sizeof(line), "ACK:%s", (s_ack == 1) ? "OK" : (s_ack == 2) ? "NO" : "--");
        if (strcmp(line, last3) != 0) { strcpy(last3, line); Display_TestLine(3, line); }

        {
            const char *st = (s_state == ST_IDLE)    ? "idle"    :
                             (s_state == ST_SENDING) ? "sending" : "error";

            if (s_auto) snprintf(line, sizeof(line), "%-12sAUTO", st);
            else        snprintf(line, sizeof(line), "%s", st);
            if (strcmp(line, last4) != 0) { strcpy(last4, line); Display_TestLine(4, line); }
        }

        Delay_ms(5);
    }
}

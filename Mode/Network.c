#include "Network.h"
#include "Config.h"
#include "Packet.h"
#include "Serial3.h"
#include "WiFiMode.h"
#include "LoRaMode.h"
#include "Serial.h"
#include "Delay.h"
#include "LowPower.h"
#include <stdlib.h>
#include "NodeMode.h"
#include "Display.h"
#include "Power.h"

uint8_t g_connType = NET_WIFI;

static uint8_t  link_was_up = 0;
static uint32_t last_hello_sec = 0;

/* ==================== 链路状态机 ==================== */

typedef enum
{
    LINK_DOWN = 0,
    LINK_CONNECTING,
    LINK_UP
} LinkState_t;

static LinkState_t link_state = LINK_DOWN;
static uint32_t    retry_at_sec = 0;   /* 下次允许重连的时刻（RTC 秒，睡眠期间也在走） */
static uint8_t     backoff_step = 0;   /* 0..5 -> 1,2,4,8,16,32 秒 */

static uint32_t stat_sent   = 0;
static uint32_t stat_fail   = 0;
static uint32_t stat_reconn = 0;

/**
  * @brief  安排下一次重连（指数退避），返回本次等待秒数
  */
static uint32_t Link_ScheduleRetry(void)
{
    uint32_t delay = (uint32_t)1 << backoff_step;

    if (delay > 32) delay = 32;
    retry_at_sec = LowPower_GetSeconds() + delay;
    if (backoff_step < 5) backoff_step++;

    return delay;
}

/**
  * @brief  真正发起一次连接（会阻塞到成功或失败）
  * @note   调用方负责退避，这里只尝试一次
  */
static uint8_t Link_TryConnect(void)
{
    uint8_t ok = 0;
    uint32_t delay;

    link_state = LINK_CONNECTING;
    Serial3_Printf("[NET] connecting %s...\r\n", (g_connType == NET_WIFI) ? "WiFi" : "LoRa");

    if (g_connType == NET_WIFI)
    {
        Serial_SetBaudRate(115200);
        Delay_ms(100);
        ok = WifiConnect();
    }
    else if (g_connType == NET_LORA)
    {
        Serial_SetBaudRate(9600);
        Delay_ms(100);
        ok = LoRaConnect();
    }

    if (ok)
    {
        link_state = LINK_UP;
        backoff_step = 0;
        Serial3_SendString("[NET] UP\r\n");
        return 1;
    }

    link_state = LINK_DOWN;
    delay = Link_ScheduleRetry();
    Serial3_Printf("[NET] connect fail, retry in %us\r\n", (unsigned int)delay);
    return 0;
}

/**
  * @brief  连接网络（启动时调用一次；也可手动调用）
  */
uint8_t Network_ConnectType(uint8_t type)
{
    /* 网络类型和 LoRa 电源在这里统一处理 */
    g_connType = (type == NET_LORA) ? NET_LORA : NET_WIFI;

    if (g_connType == NET_LORA) Power_Lora_ON();
    else                        Power_Lora_OFF();

    /* 每次连接前先把 USART1 复位到该网络的默认波特率，
       避免上一次探测/桥接留下的波特率影响这一次 */
    Serial_SetBaudRate(g_connType == NET_LORA ? 9600 : 115200);

    if (g_connType == NET_WIFI)
    {
        Serial3_Printf("WiFi: %s -> %s:%u\r\n",
                       config.wifi_ssid, config.wifi_ip,
                       (unsigned int)config.wifi_port);
    }

    if (link_state == LINK_UP) return 1;
    return Link_TryConnect();
}

uint8_t Network_Connect(void)
{
    return Network_ConnectType((g_nodeMode.net == NET_TYPE_LORA) ? NET_LORA : NET_WIFI);
}

/**
  * @brief  链路维护：主循环每轮调用，断了就按退避重连
  * @note   链路 UP 时立即返回，不做任何耗时操作
  */
void Network_Poll(void)
{
    if (link_state == LINK_UP) return;
    if (LowPower_GetSeconds() < retry_at_sec) return;

    stat_reconn++;
    Link_TryConnect();
}

/**
  * @brief  发送数据报
  * @retval 1=成功，0=失败（失败时会把链路打回 DOWN，下一轮重连）
  */
uint8_t Network_Send(const char *msg)
{
    uint8_t ok = 0;

    if (link_state != LINK_UP)
    {
        stat_fail++;
        return 0;
    }

    if (g_connType == NET_WIFI)
        ok = WifiSend(msg);
    else if (g_connType == NET_LORA)
        ok = LoRaSend(msg);

    if (ok)
    {
        stat_sent++;
        return 1;
    }

    /* 发送失败：链路状态不确定，退回 DOWN 并退避重连 */
    stat_fail++;
    link_state = LINK_DOWN;
    {
        uint32_t delay = Link_ScheduleRetry();
        Serial3_Printf("[NET] send fail, retry in %us\r\n", (unsigned int)delay);
    }
    return 0;
}

/**
  * @brief  打印链路统计（心跳时调用）
  */
void Network_PrintStats(void)
{
    Serial3_Printf("[NET] %s sent=%u fail=%u reconn=%u\r\n",
                   (link_state == LINK_UP) ? "UP" : "DOWN",
                   (unsigned int)stat_sent,
                   (unsigned int)stat_fail,
                   (unsigned int)stat_reconn);
}

/**
  * @brief  发送一条传感器读数
  * @note   WiFi 帧带 '\n'，LoRa 帧不带（正好 10 字节）
  */
uint8_t Network_SendReading(const Reading_t *reading)
{
    char frame[16];
    uint8_t newline = (g_connType == NET_LORA) ? 0 : 1;

    if (!Packet_Build(reading, config.device_id, frame, sizeof(frame), newline))
        return 0;

    {
        uint8_t ok = Network_Send(frame);

        /* 正常帧不打印：OLED 镜像下每字符约 28ms，一轮 4 帧要吃掉 1 秒多。
           只在发送失败时打出来，方便定位。 */
        if (!ok)
            Serial3_Printf("[TX] FAIL %s\r\n", frame);

        return ok;
    }
}

uint8_t Network_IsUp(void)
{
    return (link_state == LINK_UP) ? 1 : 0;
}

uint8_t Network_SendHello(void)
{
    Reading_t hello;

    hello.type  = READING_HELLO;
    hello.value = (int32_t)atoi(config.mode);   /* 心跳带 mode，中间层据此记录工作状态 */
    hello.valid = 1;

    return Network_SendReading(&hello);
}

/**
  * @brief  主循环调用：心跳 + 链路状态刷新
  * @note   刚连上发一帧心跳；纯执行器节点每 NETWORK_HEARTBEAT_SEC 秒保活
  */
void Network_Tick(void)
{
    uint32_t now_sec = LowPower_GetSeconds();
    uint8_t  up = Network_IsUp();

    if (up != link_was_up) Display_Runtime();

    if (up && !link_was_up)
    {
        Network_SendHello();
        last_hello_sec = now_sec;
    }
    else if (up)
    {
        /* WiFi：所有节点 30 秒一次，避免热点 NAT 回收空闲连接；
           LoRa：只有纯执行器节点需要保活（受占空比限制） */
        uint32_t hb = (g_connType == NET_WIFI) ? NETWORK_WIFI_HEARTBEAT_SEC
                                               : NETWORK_HEARTBEAT_SEC;

        if ((g_connType == NET_WIFI || !NodeMode_ProducesReadings()) &&
            (now_sec - last_hello_sec) >= hb)
        {
            Network_SendHello();
            last_hello_sec = now_sec;
        }
    }

    link_was_up = up;
}

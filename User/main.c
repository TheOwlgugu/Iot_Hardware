#include "stm32f10x.h"
#include "Delay.h"
#include "Millis.h"
#include "OLED.h"
#include "Serial.h"
#include "Serial3.h"
#include "W25Q64.h"
#include "Config.h"
#include "StartLink.h"
#include "NodeMode.h"
#include "Network.h"
#include "DataMode.h"
#include "LightMode.h"
#include "SystemData.h"
#include "Power.h"
#include "SoftI2C.h"

/* 调试：上电扫描 I2C 总线，确认 OLED/传感器地址；确认后改成 0 */
#define I2C_SCAN_ON_BOOT  1

int main(void)
{
    /* ========== 1. 电源脚先就位（默认全关） ========== */
    Power_Rail_Init();

    /* 2. 给 OLED + 传感器上电，等电源稳定 */
    Power_Sensor_ON();
    Delay_ms(100);

    /* 3. 总线和屏幕最先初始化，后面才能显示其他外设状态 */
    SoftI2C_Init();
    OLED_Init();
    Serial_Init();
    Serial3_Init();

#if I2C_SCAN_ON_BOOT
    /* 调试：扫描 I2C 总线，有应答的地址打印出来 */
    Serial3_SendString("I2C:");
    for (uint8_t addr = 0x08; addr < 0x78; addr++)
    {
        if (SoftI2C_Probe(addr))
            Serial3_Printf(" %02X", (unsigned int)addr);
    }
    Serial3_SendString("\r\n");
#endif

    /* 4. 配置、运行模式、毫秒计数 */
    W25Q64_Init();
    Config_Init();

    /* 开机 5 秒内从 USART3 发任意字符，进入配置模式 */
    StartLink_BootWindow(POWER_BOOT_GRACE_MS);

    NodeMode_LoadFromConfig();   // 把 config.mode 解析成运行期模式
    NodeMode_Print();            // 串口 + OLED 显示当前模式
    Millis_Init();
    SystemData_Init();
    
    /* 5. 按网络模式控制 LoRa 电源（WiFi 模式关断，避免收到 AT 指令） */
    if (g_nodeMode.net == NET_TYPE_LORA) Power_Lora_ON();
    else                                 Power_Lora_OFF();

    /* 6. 按模式初始化传感器（屏幕显示初始化进度） */
    if (!DataMode_Init())
        Serial3_SendString("Init warn\r\n");

    /* 7. RTC 最后初始化，出错时串口已经可用 */
    Power_RTC_Init();

    /* 初始化完把传感器轨断电；SCD40 周期测量需要持续供电，保持导通 */
    if (!DataMode_NeedsContinuousPower())
        Power_Sensor_OFF();

    /* EXTI & NVIC 配置 (RTC唤醒) */
    EXTI_InitTypeDef EXTI_InitStructure;
    EXTI_InitStructure.EXTI_Line = EXTI_Line17;
    EXTI_InitStructure.EXTI_Mode = EXTI_Mode_Interrupt;
    EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Rising;
    EXTI_InitStructure.EXTI_LineCmd = ENABLE;
    EXTI_Init(&EXTI_InitStructure);

    NVIC_InitTypeDef NVIC_InitStructure;
    NVIC_InitStructure.NVIC_IRQChannel = RTCAlarm_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;
    NVIC_Init(&NVIC_InitStructure);

    Serial3_SendString("System boot\r\n");

    /* ========== 网络连接（WiFi / LoRa） ========== */
    g_connType = (g_nodeMode.net == NET_TYPE_LORA) ? NET_LORA : NET_WIFI;

    if (g_connType == NET_WIFI)
    {
        Serial3_Printf("WiFi: %s -> %s:%u\r\n",
                       config.wifi_ssid, config.wifi_ip,
                       (unsigned int)config.wifi_port);
    }

    if (!Network_Connect())
        Serial3_SendString("Net fail, check wifi/ip/port/server\r\n");

    /* ========== 3. 发送周期驱动的主循环 ========== */
    // 不能用 millis() 计时：TIM2 在 STOP 下会停；这里用 RTC 秒数
    uint32_t interval_sec = (config.send_interval_ms + 999) / 1000;

    /* LoRa 至少 10 秒一帧（10 字节、SF7/8 附近约 1% 占空比）；WiFi 至少 2 秒 */
    if (g_nodeMode.net == NET_TYPE_LORA && interval_sec < 10)
        interval_sec = 10;
    if (interval_sec < 2)
        interval_sec = 2;

    uint32_t next_send_sec = Power_GetSeconds() + interval_sec;
    uint32_t tick = 0;

    while (1)
    {
        /* 3.0 链路维护：掉线按退避重连（UP 时立即返回，不耗时） */
        Network_Poll();

        /* 3.1 光照变化：EXTI 唤醒后优先处理，立刻上报，不推进发送节拍 */
        if (LightMode_EdgePending())
        {
            LightMode_ClearEdge();
            if (DataMode_LightEvent())
                Serial3_Printf("[EDGE] light=%u sent\r\n", (unsigned int)sysData.light_val);
            continue;
        }

        /* 3.2 睡到下一个发送点（光照边沿会把 MCU 提前唤醒） */
        uint32_t now = Power_GetSeconds();
        int32_t wait_sec = (int32_t)(next_send_sec - now);

        if (wait_sec > 0)
        {
            Serial3_Printf("Sleep %us\r\n", (unsigned int)wait_sec);
            EXTI_ClearITPendingBit(EXTI_Line1);   /* 清掉醒着期间积累的边沿 */
            Power_Sleep((uint32_t)wait_sec * 1000);
            continue;                              /* 醒来后回到顶部：判断是边沿还是到点 */
        }

        /* 3.3 采集 */
        tick++;
        Serial3_Printf("\r\nWake Tick:%u\r\n", (unsigned int)tick);

        /* 每 6 轮（约 1 分钟）打一次链路统计，方便判断有没有在重连 */
        if ((tick % 6) == 1)
            Network_PrintStats();

        if (NodeMode_HasAnySensor())
        {
            Power_Sensor_ON();

            /* SCD40 周期测量需要持续供电，不能采完就断 */
            if (!DataMode_NeedsContinuousPower())
                Delay_ms(DataMode_GetSettleMs());

            DataMode_Read_All();

            if (!DataMode_NeedsContinuousPower())
                Power_Sensor_OFF();
        }

        /* 3.4 发送：WiFi 一次发完所有待发；LoRa 受占空比限制，一帧一周期
         *     （光照的变化帧已经在 3.1 里走了） */
        if (g_nodeMode.net == NET_TYPE_WIFI)
            DataMode_SendPending();
        else
            DataMode_SendNext();

        /* 3.5 安排下一次发送；如果超时，就从现在重新计时 */
        next_send_sec += interval_sec;
        now = Power_GetSeconds();
        if ((int32_t)(next_send_sec - now) <= 0)
            next_send_sec = now + interval_sec;
    }
}


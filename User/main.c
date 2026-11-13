#include "stm32f10x.h"
#include "Board.h"
#include "Millis.h"
#include "Serial3.h"
#include "StartLink.h"
#include "NodeMode.h"
#include "Network.h"
#include "Display.h"
#include "DataMode.h"
#include "Downlink.h"
#include "LightMode.h"
#include "SystemData.h"
#include "Power.h"
#include "LowPower.h"
#include "Watchdog.h"

#define WDT_TIMEOUT_MS       25000
#define WDT_SLEEP_CHUNK_SEC  10

int main(void)
{
    Board_Init();

    if (Watchdog_IsResetByIWDG())
        Serial3_SendString("[WDT] last reset by IWDG\r\n");

    Display_Boot();

    /* 开机 5 秒内从 USART3 发任意字符，进入配置模式 */
    StartLink_BootWindow(LOWPOWER_BOOT_GRACE_MS);

    NodeMode_LoadFromConfig();   // 把 config.mode 解析成运行期模式
    NodeMode_Print();            // 串口 + OLED 显示当前模式
    Millis_Init();
    SystemData_Init();

    /* 按模式初始化传感器（屏幕显示初始化进度） */
    if (!DataMode_Init())
        Serial3_SendString("Init warn\r\n");

    /* RTC 最后初始化，出错时串口已经可用 */
    LowPower_Init();
    Downlink_Init();

    Serial3_SendString("System boot\r\n");

    /* ========== 网络连接（WiFi / LoRa） ========== */

    if (!Network_Connect())
        Serial3_SendString("Net fail, check wifi/ip/port/server\r\n");

    /* 联网引导完成后再断电：屏幕和传感器共用 PB0，提前断就看不到联网进度了；
       SCD40/光照模式需要持续供电，保持导通 */
    if (!DataMode_NeedsContinuousPower() && !DISPLAY_KEEP_POWER)
    {
        Power_Sensor_OFF();
        Display_SetPower(0);   /* 屏幕已断电，后续不再写 I2C */
    }
    else
    {
        Display_Runtime();     /* 屏幕还亮着，显示四路状态 */
    }

    /* ========== 主循环：按周期采集与发送 ========== */
    uint32_t interval_sec = DataMode_GetIntervalSec();

    uint32_t next_send_sec = LowPower_GetSeconds() + interval_sec;
    uint32_t tick = 0;
    uint8_t  wdt_on = 0;

    while (1)
    {
        if (wdt_on) Watchdog_Feed();

        /* 1) 链路维护：掉线按退避重连（UP 时立即返回，不耗时） */
        Network_Poll();

        /* 2) 下行：把 ESP8266 的 +IPD 解析出来并转发到对应端口（非阻塞） */
        Downlink_Poll();

        /* 3) 心跳：交给 Network 模块（连上发一帧、执行器节点周期保活） */
        Network_Tick();

        /* 成功发出第一帧后才开看门狗，联网过程不监控 */
        if (!wdt_on && Network_HasSent())
        {
            Watchdog_Init(WDT_TIMEOUT_MS);
            wdt_on = 1;
            Serial3_SendString("[WDT] on\r\n");
        }

        /* 4) 光照变化：EXTI 唤醒后优先处理，立刻上报，不推进发送节拍 */
        if (LightMode_EdgePending())
        {
            LightMode_ClearEdge();
            if (DataMode_LightEvent())
                Serial3_Printf("[EDGE] light=%u sent\r\n", (unsigned int)sysData.light_val);
            continue;
        }

        /* 5) 睡到下一个发送点（光照边沿会把 MCU 提前唤醒） */
        uint32_t now = LowPower_GetSeconds();
        int32_t wait_sec = (int32_t)(next_send_sec - now);

        if (wait_sec > 0)
        {
            /* 看门狗 25 秒：一次最多睡 10 秒，醒来喂狗再继续睡 */
            uint32_t sleep_sec = (uint32_t)wait_sec;
            if (wdt_on && sleep_sec > WDT_SLEEP_CHUNK_SEC)
                sleep_sec = WDT_SLEEP_CHUNK_SEC;

            if (sleep_sec == (uint32_t)wait_sec)
                Serial3_Printf("Sleep %us\r\n", (unsigned int)wait_sec);

            EXTI_ClearITPendingBit(EXTI_Line1);   /* 清掉醒着期间积累的边沿 */
            if (g_nodeMode.light_sleep)
                LowPower_SleepLight(sleep_sec * 1000);   /* 浅睡：时钟不停，串口能收下行 */
            else
                LowPower_Sleep(sleep_sec * 1000);        /* 深睡：STOP，省电 */

            if (wdt_on) Watchdog_Feed();
            continue;                              /* 醒来后回到顶部：判断是边沿还是到点 */
        }

        /* 6) 采集 */
        tick++;
        Serial3_Printf("\r\nWake Tick:%u\r\n", (unsigned int)tick);

        /* 每 6 轮（约 1 分钟）打一次链路统计，方便判断有没有在重连 */
        if ((tick % 6) == 1)
            Network_PrintStats();

        DataMode_Sample();

        /* 7) 发送：WiFi 一次发完，LoRa 一周期一帧 */
        DataMode_SendTick();

        /* 8) 安排下一次发送；如果超时，就从现在重新计时 */
        next_send_sec += interval_sec;
        now = LowPower_GetSeconds();
        if ((int32_t)(next_send_sec - now) <= 0)
            next_send_sec = now + interval_sec;
    }
}


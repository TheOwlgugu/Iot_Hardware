#include "DataMode.h"

PortStatus_t g_portStatus;
#include "NodeMode.h"
#include "Config.h"
#include "SystemData.h"
#include "Serial3.h"
#include "Network.h"
#include "Packet.h"
#include "LightMode.h"
#include "Delay.h"
#include "Aht21Mode.h"
#include "Ds18b20Mode.h"
#include "BuzzerMode.h"
#include "Scd40Mode.h"
#include "Pms7003Mode.h"
#include "Power.h"

/* 调试：每次采集打印光照值，发布时改成 0 */
#define DATAMODE_DEBUG_PRINT  0

/**
  * @brief  按当前模式初始化传感器
  * @retval 1=全部就绪，0=有设备未实现或未应答
  * @note   调用前 main 已经把传感器电源打开并等过稳定时间
  */
uint8_t DataMode_Init(void)
{
    uint8_t ok = 1;

    g_portStatus.data_ok  = 0;
    g_portStatus.i2c_ok   = 0;
    g_portStatus.uart2_ok = 0;

    Serial3_Printf("Init %s\r\n", g_nodeMode.raw);

    switch (g_nodeMode.data)
    {
        case DATA_PORT_LIGHT:
            LightMode_Init();
            g_portStatus.data_ok = 1;
            Serial3_SendString("Light OK\r\n");
            break;

        case DATA_PORT_BUZZER:
            BuzzerMode_Init();
            BuzzerMode_Beep(100);
            g_portStatus.data_ok = 1;
            Serial3_SendString("Buzzer OK\r\n");
            break;

        case DATA_PORT_DS18B20:
            g_portStatus.data_ok = Ds18b20Mode_Init() ? 1 : 0;
            if (g_portStatus.data_ok) Serial3_SendString("DS18B20 OK\r\n");
            else { Serial3_SendString("DS18B20 NO\r\n"); ok = 0; }
            break;

        default:
            break;
    }

    switch (g_nodeMode.i2c)
    {
        case I2C_PORT_AHT21:
            Delay_ms(100);   /* 上电稳定 */
            g_portStatus.i2c_ok = Aht21Mode_Init() ? 1 : 0;
            if (g_portStatus.i2c_ok) Serial3_SendString("AHT21 OK\r\n");
            else { Serial3_SendString("AHT21 NO\r\n"); ok = 0; }
            break;

        case I2C_PORT_SCD40:
            Delay_ms(1000);   /* 上电稳定，SCD40 需要 1 秒 */
            g_portStatus.i2c_ok = Scd40Mode_Init() ? 1 : 0;
            if (g_portStatus.i2c_ok) Serial3_SendString("SCD40 OK\r\n");
            else { Serial3_SendString("SCD40 NO\r\n"); ok = 0; }
            break;

        default:
            break;
    }

    switch (g_nodeMode.uart2)
    {
        case UART2_PORT_PMS7003:
            Pms7003Mode_Init();
            g_portStatus.uart2_ok = 1;
            Serial3_SendString("PMS7003 OK\r\n");
            break;

        default:
            break;
    }

    return ok;
}

/**
  * @brief  传感器上电后需要的稳定时间（毫秒）
  * @note   多个传感器取最大值；只算上电稳定，不含测量转换时间
  */
uint16_t DataMode_GetSettleMs(void)
{
    uint16_t ms = 0;

    switch (g_nodeMode.data)
    {
        case DATA_PORT_LIGHT:   ms = 20;  break;
        case DATA_PORT_BUZZER:  ms = 10;  break;
        case DATA_PORT_DS18B20: ms = 20;  break;
        default: break;
    }

    switch (g_nodeMode.i2c)
    {
        case I2C_PORT_AHT21: if (ms < 100)  ms = 100;  break;
        case I2C_PORT_SCD40: if (ms < 1000) ms = 1000; break;
        default: break;
    }

    /* PMS7003 外部 5V 供电，不需要在 PB0 上等上电稳定；
     * Pms7003Mode_Read 自己会等最多 1 秒收帧。 */

    return ms;
}

/**
  * @brief  当前模式是否有传感器需要持续供电
  * @retval 1=需要（SCD40 周期测量），0=可以采完就断电
  */
uint8_t DataMode_NeedsContinuousPower(void)
{
    /* SCD40 周期测量需要持续供电；PMS7003 由外部 5V 供电 */
    /* 光照模式也必须持续供电：PB0 一断，EXTI1 就收不到电平变化，中断形同虚设 */
    /* 蜂鸣器同样挂在 PB0：断电后下行能解析但不会响，必须保持供电 */
    if (g_nodeMode.data == DATA_PORT_LIGHT) return 1;
    if (g_nodeMode.data == DATA_PORT_BUZZER) return 1;

    return (g_nodeMode.i2c == I2C_PORT_SCD40);
}

void DataMode_Read_All(void)
{
    /* 不清标志：慢速传感器（如 SCD40）每 5 秒才更新一次；
     * 由 DataMode_SendNext 在发送后只清当前这一帧。 */

    /* 第1位：DATA口 */
    switch (g_nodeMode.data)
    {
        case DATA_PORT_LIGHT:
            sysData.light_val = LightMode_Read();
            sysData.light_updated = 1;
#if DATAMODE_DEBUG_PRINT
            Serial3_Printf("L=%u\r\n", (unsigned int)sysData.light_val);
#endif
            break;

        case DATA_PORT_BUZZER:
            /* 蜂鸣器只输出，不采集 */
            break;

        case DATA_PORT_DS18B20:
            {
                float t;

                if (Ds18b20Mode_Read(&t))
                {
                    sysData.temp_val = t;
                    sysData.temp_updated = 1;
#if DATAMODE_DEBUG_PRINT
                    Serial3_Printf("T%d\r\n", (int)(t * 100));
#endif
                }
            }
            break;

        default:
            break;
    }

    /* 第2位：I2C口 */
    switch (g_nodeMode.i2c)
    {
        case I2C_PORT_AHT21:
            {
                float t, h;

                if (Aht21Mode_Init() && Aht21Mode_Read(&t, &h))
                {
                    sysData.temp_val = t;
                    sysData.humi_val = h;
                    sysData.temp_updated = 1;
                    sysData.humi_updated = 1;
#if DATAMODE_DEBUG_PRINT
                    Serial3_Printf("T%d H%d\r\n", (int)(t * 100), (int)(h * 100));
#endif
                }
            }
            break;

        case I2C_PORT_SCD40:
            if (Scd40Mode_DataReady())
            {
                uint16_t co2;
                float t, h;

                if (Scd40Mode_Read(&co2, &t, &h))
                {
                    sysData.co2_val  = co2;
                    sysData.temp_val = t;
                    sysData.humi_val = h;
                    sysData.co2_updated  = 1;
                    sysData.temp_updated = 1;
                    sysData.humi_updated = 1;
#if DATAMODE_DEBUG_PRINT
                    /* 调试：T/H 为 ×100，C 为 ppm；一行不超过 16 字符 */
                    Serial3_Printf("T%d H%d C%u\r\n",
                                   (int)(t * 100), (int)(h * 100), (unsigned int)co2);
#endif
                }
            }
            break;

        default:
            break;
    }

    /* 第3位：USART2口 */
    switch (g_nodeMode.uart2)
    {
        case UART2_PORT_PMS7003:
            {
                uint16_t pm25;

                if (Pms7003Mode_Read(&pm25))
                {
                    sysData.pm25_val = pm25;
                    sysData.pm25_updated = 1;
#if DATAMODE_DEBUG_PRINT
                    Serial3_Printf("PM=%u\r\n", (unsigned int)pm25);
#endif
                }
            }
            break;

        default:
            break;
    }
}

/**
  * @brief  把一条读数交给网络层编码/发送
  */
static uint8_t DataMode_IsPending(uint8_t type)
{
    switch (type)
    {
        case READING_LIGHT: return sysData.light_updated;
        case READING_TEMP:  return sysData.temp_updated;
        case READING_HUMI:  return sysData.humi_updated;
        case READING_CO2:   return sysData.co2_updated;
        case READING_PM25:  return sysData.pm25_updated;
        default:            return 0;
    }
}

static int32_t DataMode_Value(uint8_t type)
{
    switch (type)
    {
        case READING_LIGHT: return (int32_t)sysData.light_val;
        case READING_TEMP:  return (int32_t)(sysData.temp_val * 100);
        case READING_HUMI:  return (int32_t)(sysData.humi_val * 100);
        case READING_CO2:   return (int32_t)sysData.co2_val;
        case READING_PM25:  return (int32_t)sysData.pm25_val;
        default:            return 0;
    }
}

static void DataMode_ClearPending(uint8_t type)
{
    switch (type)
    {
        case READING_LIGHT: sysData.light_updated = 0; break;
        case READING_TEMP:  sysData.temp_updated  = 0; break;
        case READING_HUMI:  sysData.humi_updated  = 0; break;
        case READING_CO2:   sysData.co2_updated   = 0; break;
        case READING_PM25:  sysData.pm25_updated  = 0; break;
        default: break;
    }
}

/* 上一次已经发出的光照值（0xFF=还没发过），用于边沿事件去重 */
static uint8_t last_light_sent = 0xFF;

static uint8_t DataMode_SendOne(uint8_t type, int32_t value)
{
    Reading_t reading;

    reading.type  = type;
    reading.value = value;
    reading.valid = 1;

    return Network_SendReading(&reading);
}

/**
  * @brief  按固定顺序发送下一个待发传感器（一帧一传感器）
  * @retval 1=发了一帧，0=没有待发数据
  * @note   顺序：光照 → 温度 → 湿度 → CO2 → PM2.5
  *         发送后只清这一帧的标志，其他传感器继续等待轮询
  */
uint8_t DataMode_SendNext(void)
{
    static const uint8_t order[5] = {
        READING_LIGHT, READING_TEMP, READING_HUMI, READING_CO2, READING_PM25
    };
    static uint8_t next = 0;
    uint8_t i, idx;

    for (i = 0; i < 5; i++)
    {
        idx = (uint8_t)((next + i) % 5);

        if (DataMode_IsPending(order[idx]))
        {
            int32_t v = DataMode_Value(order[idx]);

            if (!DataMode_SendOne(order[idx], v))
                return 0;          /* 发送失败：保留 pending，下次重试 */

            if (order[idx] == READING_LIGHT) last_light_sent = (uint8_t)v;
            DataMode_ClearPending(order[idx]);
            next = (uint8_t)((idx + 1) % 5);
            return 1;
        }
    }

    return 0;
}

/**
  * @brief  光照变化事件：去抖后立刻发送，不等 10 秒轮询
  * @retval 1=已发送，0=值没变或发送失败
  * @note   由主循环在 EXTI1 唤醒后调用；和轮询共用 last_light_sent 去重
  */
uint8_t DataMode_LightEvent(void)
{
    uint8_t v;

    Delay_ms(20);                  /* 去抖 */
    v = LightMode_Read();

    sysData.light_val = v;
    sysData.light_updated = 1;

    if (v == last_light_sent)      /* 和上次发的一样，不重复发 */
        return 0;

    if (!DataMode_SendOne(READING_LIGHT, (int32_t)v))
        return 0;                  /* 发送失败：保留 pending，下一轮批量补发 */

    last_light_sent = v;
    sysData.light_updated = 0;     /* 已经发出，轮询不再补发 */
    return 1;
}

/**
  * @brief  一次把当前所有待发数据都发完（WiFi 模式用）
  * @retval 实际发出的帧数
  * @note   WiFi 带宽足够，每轮唤醒把 4~5 帧连续发出去，
  *         这样每个传感器的刷新周期等于唤醒周期（10 秒），而不是 10N 秒。
  *         LoRa 受占空比限制，仍然用 DataMode_SendNext 一帧一周期。
  */
uint8_t DataMode_SendPending(void)
{
    static const uint8_t order[5] = {
        READING_LIGHT, READING_TEMP, READING_HUMI, READING_CO2, READING_PM25
    };
    uint8_t i, sent = 0;

    for (i = 0; i < 5; i++)
    {
        uint8_t t = order[i];
        int32_t v;

        if (!DataMode_IsPending(t)) continue;

        v = DataMode_Value(t);

        if (!DataMode_SendOne(t, v))
            continue;              /* 发送失败：保留 pending，下一轮再试 */

        if (t == READING_LIGHT) last_light_sent = (uint8_t)v;
        DataMode_ClearPending(t);
        sent++;
    }

    return sent;
}

/**
  * @brief  计算发送周期（秒），含各链路的最小间隔
  */
uint32_t DataMode_GetIntervalSec(void)
{
    uint32_t sec = (config.send_interval_ms + 999) / 1000;

    if (g_nodeMode.net == NET_TYPE_LORA && sec < 10) sec = 10;
    if (sec < 2) sec = 2;
    return sec;
}

/**
  * @brief  一次采集：上电 -> 稳定 -> 读取 -> 断电（需要持续供电的除外）
  */
void DataMode_Sample(void)
{
    if (!NodeMode_ProducesReadings()) return;

    Power_Sensor_ON();

    if (!DataMode_NeedsContinuousPower())
        Delay_ms(DataMode_GetSettleMs());

    DataMode_Read_All();

    if (!DataMode_NeedsContinuousPower())
        Power_Sensor_OFF();
}

/**
  * @brief  按网络类型发送：WiFi 一次发完，LoRa 一周期一帧
  */
void DataMode_SendTick(void)
{
    if (g_nodeMode.net == NET_TYPE_WIFI)
        DataMode_SendPending();
    else
        DataMode_SendNext();
}

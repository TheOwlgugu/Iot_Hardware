#include "Ra08_Driver.h"
#include "Watchdog.h"
#include "Serial.h"
#include "Delay.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <Serial3.h>

#define RA08_BAUD    9600
#define RA08_EOL     "\r\n"
#define RA08_PING    "AT+CGMI?"   /* 这个固件不支持裸 AT（回 +CME ERROR:1），用 CGMI 查询做存活检测 */
#define RX_BUF_SIZE  256

static char     rx_buf[RX_BUF_SIZE];
static uint16_t rx_index = 0;
static uint8_t  joined   = 0;

static void ClearRxBuffer(void)
{
    /* 同时清 USART1 环形缓冲：模组启动横幅有 300+ 字节，
       不清掉会混进本次响应，甚至把应答挤掉 */
    while (Serial_Available())
        (void)Serial_GetByte();

    memset(rx_buf, 0, RX_BUF_SIZE);
    rx_index = 0;
}

/* 返回 1:匹配 ok  2:匹配 fail  0:超时。ok/fail 传 NULL 则只是收数据 */
static uint8_t WaitFor(const char *ok, const char *fail, uint32_t timeout_ms)
{
    for (uint32_t t = 0; t < timeout_ms; t++)
    {
        Watchdog_Feed();
        while (Serial_Available())
        {
            if (rx_index >= RX_BUF_SIZE - 1)   /* 满了丢掉旧的一半 */
            {
                memmove(rx_buf, rx_buf + rx_index - RX_BUF_SIZE / 2, RX_BUF_SIZE / 2);
                rx_index = RX_BUF_SIZE / 2;
            }
            rx_buf[rx_index++] = Serial_GetByte();
            rx_buf[rx_index] = '\0';

            if (ok   && strstr(rx_buf, ok))   return 1;
            if (fail && strstr(rx_buf, fail)) return 2;
        }
        Delay_ms(1);
    }
    return 0;
}

uint8_t Ra08_SendCmd(const char *cmd, const char *expected, uint32_t timeout_ms)
{
    ClearRxBuffer();
    Serial_SendString((char *)cmd);
    return WaitFor(expected, "ERROR", timeout_ms) == 1;
}

/* 打印响应里第一条以 '+' 开头的行（跳过命令回显） */
static void DumpRxHex(void);

static void PrintResponse(const char *tag)
{
    char *p = strchr(rx_buf, '+');
    char line[64];
    uint8_t n = 0;

    if (!p) p = rx_buf;
    while (*p && *p != '\r' && *p != '\n' && n < sizeof(line) - 1)
        line[n++] = *p++;
    line[n] = '\0';

    {
        uint8_t i;
        uint8_t printable = 1;

        for (i = 0; i < n; i++)
        {
            if (line[i] < 0x20 || line[i] > 0x7E) { printable = 0; break; }
        }

        if (printable)
        {
            Serial3_Printf("[LoRa] %s %s\r\n", tag, line);
        }
        else
        {
            Serial3_Printf("[LoRa] %s <non-ascii>\r\n", tag);
            DumpRxHex();
        }
    }
}

/* ==================== 波特率探测 ==================== */

static uint32_t ra08_baud = RA08_BAUD;   /* 本次运行使用的波特率 */
static uint8_t  ra08_confirmed = 0;      /* 初始化成功过：后续重连跳过启动等待和波特率扫描 */

/**
  * @brief  打印最后一次收到的原始字节（诊断用）
  */
static void DumpRxHex(void)
{
    const char *rx = rx_buf;
    uint16_t n = 0;

    Serial3_SendString("[LoRa] RX hex:");
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
  * @brief  存活检测：不同固件支持的命令不一样
  * @note   先试 AT+CGMI?（新固件），失败再试裸 AT（部分老固件不支持 CGMI）
  */
static uint8_t Ra08_Ping(void)
{
    if (Ra08_SendCmd("AT+CGMI?" RA08_EOL, "OK", 800)) return 1;
    if (Ra08_SendCmd("AT" RA08_EOL, "OK", 800))       return 1;
    return 0;
}

/**
  * @brief  在指定波特率上试一次 AT
  */
static uint8_t TryBaud(uint32_t baud)
{
    Serial_SetBaudRate(baud);
    Delay_ms(50);
    return Ra08_Ping();
}
/**
  * @brief  十六进制字符转数值
  */
static int8_t HexVal(char c)
{
    if (c >= '0' && c <= '9') return (int8_t)(c - '0');
    if (c >= 'A' && c <= 'F') return (int8_t)(c - 'A' + 10);
    if (c >= 'a' && c <= 'f') return (int8_t)(c - 'a' + 10);
    return -1;
}
uint8_t Ra08_Init(void)
{
    uint8_t i;
    uint8_t ok = 0;

    Serial_SetBaudRate(ra08_baud);   /* 第一次默认 9600，探测到别的就记住 */
    Delay_ms(ra08_confirmed ? 100 : 1500);   /* 只有首次上电才需要等启动横幅 */
    ClearRxBuffer();                 /* 丢掉横幅残留，连同硬件环形缓冲 */

    /* 模组启动后第一次 AT 也可能不应答，多试几次 */
    for (i = 0; i < (ra08_confirmed ? 3 : 8); i++)
    {
        if (Ra08_Ping())
        {
            ok = 1;
            break;
        }
        Delay_ms(200);
    }

    if (!ok && !ra08_confirmed)
    {
        /* 新模组出厂波特率不一定是 9600，逐个试一遍；结果记在 ra08_baud */
        static const uint32_t bauds[] = {115200, 57600, 38400, 19200, 74880};
        uint8_t k;

        Serial3_SendString("[LoRa] no AT at 9600, probing baud...\r\n");

        for (k = 0; k < sizeof(bauds) / sizeof(bauds[0]); k++)
        {
            if (TryBaud(bauds[k]))
            {
                ra08_baud = bauds[k];
                ok = 1;
                break;
            }
        }

        /* 探针都没中，最后回到 9600 再给一次长超时（容忍模组启动慢） */
        if (!ok)
        {
            Serial_SetBaudRate(9600);
            Delay_ms(500);
            if (Ra08_Ping())
            {
                ra08_baud = 9600;
                ok = 1;
            }
        }

        if (ok)
        {
            Serial3_Printf("[LoRa] AT OK at %u baud\r\n", (unsigned int)ra08_baud);
            Serial_SetBaudRate(ra08_baud);
        }
        else
        {
            DumpRxHex();
            Serial3_SendString("[LoRa] no AT on any baud, check 3V3/TX-RX/GND\r\n");
            Serial_SetBaudRate(RA08_BAUD);
            return 0;
        }
    }

    /* 显式设定：OTAA、正常工作模式、应用端口 10 */
    Ra08_SendCmd("AT+CJOINMODE=0" RA08_EOL, "OK", 1000);
    Ra08_SendCmd("AT+CWORKMODE=2" RA08_EOL, "OK", 1000);
    Ra08_SendCmd("AT+CAPPPORT=10" RA08_EOL, "OK", 1000);

    ra08_confirmed = 1;

    Serial3_SendString("[LoRa] Init OK\r\n");
    return 1;
}
/**
  * @brief  从模块的 Rx buffer 取一帧下行（AT+DRX?）
  * @param  out       输出缓冲（ASCII 数据）
  * @param  out_size  缓冲大小
  * @param  out_len   实际长度
  * @retval 1=取到数据，0=没有
  * @note   模块会在每次上行后的接收窗口把下行存进 buffer，
  *         这里取一次并清空，不需要在 RX 窗口一直等着。
  */
uint8_t Ra08_ReadDownlink(char *out, uint16_t out_size, uint16_t *out_len)
{
    char *p;
    char *comma;
    uint16_t len;
    uint16_t n = 0;

    if (out_len) *out_len = 0;
    if (out == 0 || out_size == 0) return 0;

    ClearRxBuffer();
    Serial_SendString("AT+DRX?" RA08_EOL);

    if (WaitFor("OK", "ERROR", 2000) != 1) return 0;

    p = strstr(rx_buf, "+DRX:");
    if (p == 0) return 0;

    p += 5;
    len = (uint16_t)atoi(p);
    if (len == 0) return 0;

    comma = strchr(p, ',');
    if (comma == 0) return 0;
    comma++;

    while (n < len && n < out_size && comma[0] != '\0' && comma[1] != '\0')
    {
        int8_t hi = HexVal(comma[0]);
        int8_t lo = HexVal(comma[1]);
        if (hi < 0 || lo < 0) break;
        out[n++] = (char)((hi << 4) | lo);
        comma += 2;
    }

    if (out_len) *out_len = n;
    return (n > 0) ? 1 : 0;
}
/* 只在第一次烧录/换密钥/换模组时调用,CSAVE 会写 Flash,不要每次开机都调 */
static uint8_t ProvCmd(const char *cmd, uint32_t timeout_ms)
{
    if (Ra08_SendCmd(cmd, "OK", timeout_ms)) return 1;

    Serial3_SendString("[LoRa] prov fail: ");
    Serial3_SendString((char *)cmd);
    DumpRxHex();
    return 0;
}

uint8_t Ra08_Provision(const char *deveui, const char *appeui, const char *appkey)
{
    char cmd[80];

    sprintf(cmd, "AT+CDEVEUI=%s" RA08_EOL, deveui);
    if (!ProvCmd(cmd, 1000)) return 0;

    sprintf(cmd, "AT+CAPPEUI=%s" RA08_EOL, appeui);
    if (!ProvCmd(cmd, 1000)) return 0;

    sprintf(cmd, "AT+CAPPKEY=%s" RA08_EOL, appkey);
    if (!ProvCmd(cmd, 1000)) return 0;

    if (!ProvCmd("AT+CCLASS=0" RA08_EOL, 1000)) return 0;
    if (!ProvCmd("AT+CFREQBANDMASK=0001" RA08_EOL, 1000)) return 0;
    if (!ProvCmd("AT+CJOINMODE=0" RA08_EOL, 1000)) return 0;
    if (!ProvCmd("AT+CSAVE" RA08_EOL, 3000)) return 0;

    return 1;
}

uint8_t Ra08_Join(void)
{
    joined = 0;

    if (!Ra08_SendCmd("AT+CJOIN=1,0,10,8" RA08_EOL, "OK", 2000))
        return 0;

    /* OK 只表示命令被接受,结果稍后以 +CJOIN:OK / +CJOIN:FAIL 上报;8 次 x 10 秒 + 余量 */
    if (WaitFor("+CJOIN:OK", "+CJOIN:FAIL", 100000) == 1)
    {
        joined = 1;
        return 1;
    }

    return 0;
}

uint8_t Ra08_IsJoined(void)
{
    return joined;
}

Ra08_SendResult Ra08_SendData(const uint8_t *data, uint8_t len, uint8_t confirm)
{
    char cmd[128];
    int n;

    if (len == 0 || len > RA08_MAX_PAYLOAD) return RA08_SEND_TOO_LONG;
    if (!joined) return RA08_SEND_NOT_JOINED;

    n = sprintf(cmd, "AT+DTRX=%d,1,%d,", confirm ? 1 : 0, len);
    for (uint8_t i = 0; i < len; i++)
        n += sprintf(cmd + n, "%02X", data[i]);
    strcpy(cmd + n, RA08_EOL);

    ClearRxBuffer();
    Serial_SendString(cmd);

    uint8_t r = WaitFor("OK+SEND", "ERR+SEND", 3000);
    if (r == 0) return RA08_SEND_TIMEOUT;
    if (r == 2)
    {
        WaitFor(NULL, NULL, 30);   /* 等错误码那几个字符到齐 */
        char *p = strstr(rx_buf, "ERR+SEND:");
        char *end = NULL;
        long code = -1;
        if (p) { code = strtol(p + 9, &end, 10); if (end == p + 9) code = -1; }

        if (code == 0) { joined = 0; return RA08_SEND_NOT_JOINED; }
        if (code == 2) return RA08_SEND_TOO_LONG;
        return RA08_SEND_BUSY;
    }

    /* 确认帧要等网关应答,最长约十几秒 */
    r = WaitFor("OK+SENT", "ERR+SENT", confirm ? 15000 : 5000);
    if (r == 1) return RA08_SEND_OK;
    if (r == 2) return RA08_SEND_NO_ACK;
    return RA08_SEND_TIMEOUT;
}

static void InfoCmd(const char *cmd, const char *tag)
{
    if (Ra08_SendCmd(cmd, "OK", 1500))
    {
        PrintResponse(tag);
        return;
    }

    Serial3_SendString("[LoRa] ");
    Serial3_SendString((char *)tag);
    Serial3_SendString("FAIL ");
    DumpRxHex();
}

void Ra08_PrintInfo(void)
{
    InfoCmd("AT+CGMR?" RA08_EOL, "VER    ");
    InfoCmd("AT+CDEVEUI?" RA08_EOL, "DEVEUI ");
    InfoCmd("AT+CAPPEUI?" RA08_EOL, "APPEUI ");
    InfoCmd("AT+CCLASS?" RA08_EOL, "CLASS  ");
    InfoCmd("AT+CFREQBANDMASK?" RA08_EOL, "MASK   ");
}

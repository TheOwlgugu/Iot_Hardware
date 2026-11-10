#include "StartLink.h"
#include "TestMode.h"
#include "Display.h"
#include "Config.h"
#include "Serial3.h"
#include "Serial.h"
#include "Power.h"
#include "Delay.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

#define FIELD_MATCH(name)  (field_len == strlen(name) && strncmp(field, name, field_len) == 0)
#define FIELD_MATCH_UNDERSCORE(name) (field_len == strlen(name) + 1 && field[0] == '_' && strncmp(field + 1, name, strlen(name)) == 0)
#define FIELD_IS(short1, short2, longname) (FIELD_MATCH(short1) || FIELD_MATCH(short2) || FIELD_MATCH_UNDERSCORE(short1) || FIELD_MATCH(longname))

/* 配置改动只写 RAM，commit/exit 时一次性擦写 Flash（P0-2） */
static uint8_t cfg_dirty = 0;

static void SaveAndReply(const char *field, int field_len, const char *value)
{
    cfg_dirty = 1;
    Serial3_Printf("OK: %.*s = %s (pending)\r\n", field_len, field, value);
}

uint8_t SetConfig(char *cmd)
{
    if (strncmp(cmd, "set:", 4) != 0 && strncmp(cmd, "SET:", 4) != 0) return 0;

    char *eq = strchr(cmd, '=');
    if (eq == NULL) return 0;

    char *field = cmd + 4;
    uint8_t field_len = eq - field;
    char *value = eq + 1;

    uint8_t val_len = strlen(value);
    while (val_len > 0 && (value[val_len - 1] == '\r' || value[val_len - 1] == '\n')) {
        value[--val_len] = '\0';
    }

    uint8_t matched = 0;

    /* ---------- device_id ---------- */
    if (FIELD_IS("id", "ID", "device_id"))
    {
        if (strlen(value) != 3) { Serial3_SendString("ERR: id must be 3 digits\r\n"); return 0; }
        strncpy(config.device_id, value, sizeof(config.device_id) - 1);
        matched = 1;
    }
    /* ---------- mode（4位组合校验） ---------- */
    else if (FIELD_IS("mode", "MODE", "mode"))
    {
        if (strlen(value) != 4) {
            Serial3_SendString("ERR: mode must be 4 digits (e.g. 3121)\r\n");
            return 0;
        }
        // DATA口(0~3), I2C口(0~2), USART2(0~1), NET(0~3)
        // NET: 0=WiFi深睡 1=LoRa深睡 2=WiFi浅睡 3=LoRa浅睡
        if (value[0] < '0' || value[0] > '3' ||
            value[1] < '0' || value[1] > '2' ||
            value[2] < '0' || value[2] > '1' ||
            value[3] < '0' || value[3] > '3') 
        {
            Serial3_SendString("ERR: mode format invalid\r\n");
            return 0;
        }
        strncpy(config.mode, value, sizeof(config.mode) - 1);
        matched = 1;
    }
    /* ---------- interval（发信间隔） ---------- */
    else if (FIELD_IS("interval", "INTERVAL", "send_interval"))
    {
        uint32_t ms = (uint32_t)atol(value);
        if (ms < 6000) {
            Serial3_SendString("ERR: interval must be >= 6000 ms (LoRa limit)\r\n");
            return 0;
        }
        config.send_interval_ms = ms;
        matched = 1;
    }
    /* ---------- WiFi 配置 ---------- */
    else if (FIELD_IS("wifi", "WIFI", "wifi_ssid"))
    {
        strncpy(config.wifi_ssid, value, sizeof(config.wifi_ssid) - 1);
        matched = 1;
    }
    else if (FIELD_IS("pwd", "PWD", "wifi_pwd"))
    {
        strncpy(config.wifi_pwd, value, sizeof(config.wifi_pwd) - 1);
        matched = 1;
    }
    else if (FIELD_IS("ip", "IP", "wifi_ip"))
    {
        strncpy(config.wifi_ip, value, sizeof(config.wifi_ip) - 1);
        matched = 1;
    }
    else if (FIELD_IS("port", "PORT", "wifi_port"))
    {
        config.wifi_port = (uint16_t)atoi(value);
        matched = 1;
    }
    /* ---------- LoRa 配置 ---------- */
    else if (FIELD_IS("deveui", "DEVEUI", "lora_deveui"))
    {
        if (strlen(value) != 16) { Serial3_SendString("ERR: deveui must be 16 hex chars\r\n"); return 0; }
        strncpy(config.lora_deveui, value, sizeof(config.lora_deveui) - 1);
        matched = 1;
    }
    else if (FIELD_IS("appeui", "APPEUI", "lora_appeui"))
    {
        if (strlen(value) != 16) { Serial3_SendString("ERR: appeui must be 16 hex chars\r\n"); return 0; }
        strncpy(config.lora_appeui, value, sizeof(config.lora_appeui) - 1);
        matched = 1;
    }
    else if (FIELD_IS("appkey", "APPKEY", "lora_appkey"))
    {
        if (strlen(value) != 32) { Serial3_SendString("ERR: appkey must be 32 hex chars\r\n"); return 0; }
        strncpy(config.lora_appkey, value, sizeof(config.lora_appkey) - 1);
        matched = 1;
    }
    else if (FIELD_IS("class", "CLASS", "lora_class"))
    {
        uint8_t c = (uint8_t)atoi(value);
        if (c > 2) { Serial3_SendString("ERR: class must be 0/1/2\r\n"); return 0; }
        config.lora_class = c;
        matched = 1;
    }
    /* ---------- 强制重新写密钥（换模组时用） ---------- */
    else if (FIELD_IS("token", "TOKEN", "device_token"))
    {
        uint8_t i;
        uint8_t ok = (strlen(value) == 32);

        for (i = 0; ok && i < 32; i++)
            if (!isxdigit((unsigned char)value[i])) ok = 0;

        if (!ok) { Serial3_SendString("ERR: token must be 32 hex chars\r\n"); return 0; }

        Config_SetToken(value);
        matched = 1;
    }
    else if (FIELD_IS("provisioned", "PROVISIONED", "lora_provisioned"))
    {
        uint8_t p = (uint8_t)atoi(value);
        if (p > 1) { Serial3_SendString("ERR: provisioned must be 0/1\r\n"); return 0; }
        config.lora_provisioned = p;
        matched = 1;
    }

    if (matched) { SaveAndReply(field, field_len, value); return 1; }
    else { Serial3_SendString("ERR: unknown field\r\n"); return 0; }
}

static void ShowConfig(void)
{
    Serial3_Printf("id=%s\r\n",       config.device_id);
    Serial3_Printf("mode=%s\r\n",     config.mode);
    Serial3_Printf("interval=%d\r\n", config.send_interval_ms);  // 注意这里名字变了
    Serial3_Printf("wifi=%s\r\n",     config.wifi_ssid);
    Serial3_Printf("pwd=%s\r\n",      config.wifi_pwd);
    Serial3_Printf("ip=%s\r\n",       config.wifi_ip);
    Serial3_Printf("port=%d\r\n",     config.wifi_port);
    Serial3_Printf("deveui=%s\r\n",   config.lora_deveui);
    Serial3_Printf("appeui=%s\r\n",   config.lora_appeui);
    Serial3_Printf("appkey=%s\r\n",   config.lora_appkey);
    Serial3_Printf("class=%d\r\n",    config.lora_class);
    Serial3_Printf("provisioned=%d\r\n", config.lora_provisioned);
    /* 配置串口只在本机使用，token 直接显示，方便核对 */
    Serial3_Printf("token=%s\r\n", config_token[0] ? config_token : "(none)");
}

void StartLink(void)
{
    char cmd_buf[128];
    uint8_t cmd_idx = 0;
    uint32_t idle_ticks = 0;   /* 10 ms/tick，30000 tick = 5 分钟无输入自动退出 */

    /* 配置模式要快：先关掉 OLED 镜像，否则 700 字符的横幅要打印 20 秒（P0-1） */

    Serial3_SendString("\r\n=== Config Mode ===\r\n");
    Serial3_SendString("Commands:\r\n");
    Serial3_SendString("  set:id=001\r\n");
    Serial3_SendString("  set:mode=XXXX  (DATA 0-3, I2C 0-2, UART 0-1, NET 0-3)\r\n");
    Serial3_SendString("                 (1=Light, 2=Buzzer, 3=DS18B20)\r\n");
    Serial3_SendString("                 (1=AHT21, 2=SCD40)\r\n");
    Serial3_SendString("                 (1=PMS7003)\r\n");
    Serial3_SendString("                 (0=WiFi deep, 1=LoRa deep)\r\n");
    Serial3_SendString("                 (2=WiFi light, 3=LoRa light)\r\n");
    Serial3_SendString("  set:interval=60000  (ms, >= 6000)\r\n");
    Serial3_SendString("  set:wifi=SSID\r\n");
    Serial3_SendString("  set:pwd=PASSWORD\r\n");
    Serial3_SendString("  set:ip=192.168.1.100\r\n");
    Serial3_SendString("  set:port=4000\r\n");
    Serial3_SendString("  set:deveui=16 hex\r\n");
    Serial3_SendString("  set:appeui=16 hex\r\n");
    Serial3_SendString("  set:appkey=32 hex\r\n");
    Serial3_SendString("  set:class=0/1/2\r\n");
    Serial3_SendString("  set:provisioned=0/1  (0=next boot rewrites LoRa keys)\r\n");
    Serial3_SendString("  set:token=32 hex  (device auth, required by server)\r\n");
    Serial3_SendString("  test:net=0/1 - WiFi/LoRa network test (reboot to exit)\r\n");
    Serial3_SendString("  test:bridge=9600 - USART3 <-> RA-08 passthrough (ESC exit)\r\n");
    Serial3_SendString("  get    - show config\r\n");
    Serial3_SendString("  commit - save to flash\r\n");
    Serial3_SendString("  exit   - save (if changed) and quit\r\n");

    while (1)
    {
        if (Serial3_Available())
        {
            uint8_t ch = Serial3_GetByte();
            idle_ticks = 0;
            Serial3_SendByte(ch);
            if (ch == '\r' || ch == '\n')
            {
                if (cmd_idx == 0) continue;
                cmd_buf[cmd_idx] = '\0';

                if (strcmp(cmd_buf, "exit") == 0 || strcmp(cmd_buf, "EXIT") == 0) {
                    if (cfg_dirty) { Config_Save(); Serial3_SendString("\r\nSaved.\r\n"); }
                    Display_Boot();
                    Serial3_SendString("\r\nExiting...\r\n");
                    return;
                }
                else if (strcmp(cmd_buf, "commit") == 0 || strcmp(cmd_buf, "COMMIT") == 0) {
                    Config_Save();
                    cfg_dirty = 0;
                    Display_Boot();
                    Serial3_SendString("OK: saved\r\n");
                }
                else if (strncmp(cmd_buf, "test:", 5) == 0 || strncmp(cmd_buf, "TEST:", 5) == 0) {
                    if (!TestMode_Choose(cmd_buf))
                        Serial3_SendString("ERR: test:net=0|1 / test:bridge=9600\r\n");
                }
                else if (strcmp(cmd_buf, "get") == 0 || strcmp(cmd_buf, "GET") == 0) {
                    ShowConfig();
                }
                else if (strncmp(cmd_buf, "set:", 4) == 0 || strncmp(cmd_buf, "SET:", 4) == 0) {
                    SetConfig(cmd_buf);
                }
                else { Serial3_SendString("ERR: unknown command\r\n"); }
                cmd_idx = 0; cmd_buf[0] = '\0';
            }
            else {
                if (cmd_idx < sizeof(cmd_buf) - 1) cmd_buf[cmd_idx++] = ch;
            }
        }
        else
        {
            /* 无输入时按 10 ms 节拍累加：超时自动提交并退出，避免板子卡死在配置模式 */
            Delay_ms(10);
            if (++idle_ticks > 30000)
            {
                if (cfg_dirty) { Config_Save(); Serial3_SendString("\r\nSaved.\r\n"); }
                Display_Boot();
                Serial3_SendString("\r\nConfig timeout, exiting...\r\n");
                return;
            }
        }
    }
}

/**
  * @brief  开机配置窗口
  * @param  ms  等待时间（毫秒）
  * @note   收到任意字节就把该字节丢掉并进入 StartLink，避免把噪声当成命令
  */
void StartLink_BootWindow(uint16_t ms)
{
    Serial3_Printf("Cfg? %u ms\r\n", (unsigned int)ms);

    for (uint16_t i = 0; i < ms / 10; i++)
    {
        if (Serial3_Available())
        {
            (void)Serial3_GetByte();
            StartLink();
            return;
        }
        Delay_ms(10);
    }
}

#include "Config.h"
#include "W25Q64.h"
#include <string.h>

Config_t config;
char     config_token[33];

#pragma pack(1)
typedef struct {
    uint32_t magic;
    char     token[33];
    uint8_t  crc;
} TokenCfg_t;
#pragma pack()

static uint8_t CRC8(const uint8_t *data, uint32_t len)
{
    uint8_t crc = 0x00;
    while (len--)
    {
        crc ^= *data++;
        for (uint8_t i = 0; i < 8; i++)
        {
            if (crc & 0x80) crc = (crc << 1) ^ 0x07;
            else crc <<= 1;
        }
    }
    return crc;
}

void Config_SetDefault(void)
{
    memset(&config, 0, sizeof(Config_t));
    config.magic = CONFIG_MAGIC;

    strncpy(config.device_id, "001", sizeof(config.device_id) - 1);
    
    /* ★ 默认模式：0000 (全关，无网络) */
    strncpy(config.mode, "0000", sizeof(config.mode) - 1); 
    
    /* ★ 默认发送间隔：60秒 */
    config.send_interval_ms = 60000; 

    /* WiFi 默认值 */
    strncpy(config.wifi_ssid, "main",   sizeof(config.wifi_ssid) - 1);
    strncpy(config.wifi_pwd,  "admin1234", sizeof(config.wifi_pwd)  - 1);
    strncpy(config.wifi_ip,   "192.168.1.100", sizeof(config.wifi_ip)   - 1);
    config.wifi_port = 4000;

    /* LoRa 默认值 */
    strncpy(config.lora_deveui, "1d4517b623310c86", sizeof(config.lora_deveui) - 1);
    strncpy(config.lora_appeui, "248df2c2303ac457", sizeof(config.lora_appeui) - 1);
    strncpy(config.lora_appkey, "8a5b1303a0ccc45ac2b9aca1263d2c27", sizeof(config.lora_appkey) - 1);
    config.lora_class = 0;
    config.lora_provisioned = 0;
}

void Config_SetToken(const char *token)
{
    if (token == 0) { config_token[0] = '\0'; return; }
    strncpy(config_token, token, sizeof(config_token) - 1);
    config_token[sizeof(config_token) - 1] = '\0';
}

static void Config_SaveToken(void)
{
    TokenCfg_t tc;

    memset(&tc, 0, sizeof(tc));
    tc.magic = TOKEN_MAGIC;
    strncpy(tc.token, config_token, sizeof(tc.token) - 1);
    tc.crc = CRC8((uint8_t*)&tc, sizeof(TokenCfg_t) - 1);

    W25Q64_SectorErase(TOKEN_FLASH_ADDR);
    W25Q64_PageProgram(TOKEN_FLASH_ADDR, (uint8_t*)&tc, sizeof(TokenCfg_t));
}

static void Config_LoadToken(void)
{
    TokenCfg_t tc;

    W25Q64_ReadData(TOKEN_FLASH_ADDR, (uint8_t*)&tc, sizeof(TokenCfg_t));
    if (tc.magic == TOKEN_MAGIC && tc.crc == CRC8((uint8_t*)&tc, sizeof(TokenCfg_t) - 1))
    {
        memcpy(config_token, tc.token, sizeof(config_token));
        config_token[sizeof(config_token) - 1] = '\0';
    }
    else
    {
        config_token[0] = '\0';
    }
}

void Config_Save(void)
{
    config.crc = CRC8((uint8_t*)&config, sizeof(Config_t) - 1);
    W25Q64_SectorErase(CONFIG_FLASH_ADDR);
    W25Q64_PageProgram(CONFIG_FLASH_ADDR, (uint8_t*)&config, sizeof(Config_t));
    Config_SaveToken();
}

void Config_Init(void)
{
    W25Q64_ReadData(CONFIG_FLASH_ADDR, (uint8_t*)&config, sizeof(Config_t));
    uint8_t crc_calc = CRC8((uint8_t*)&config, sizeof(Config_t) - 1);
    if (config.magic != CONFIG_MAGIC || config.crc != crc_calc)
    {
        Config_SetDefault();
        Config_Save();
    }

    Config_LoadToken();
}

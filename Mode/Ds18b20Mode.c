#include "Ds18b20Mode.h"
#include "OneWire.h"
#include "Delay.h"

#define DS18B20_CMD_SKIP_ROM        0xCC
#define DS18B20_CMD_CONVERT_T       0x44
#define DS18B20_CMD_READ_SCRATCHPAD 0xBE

/**
  * @brief  初始化 1-Wire 并检测 DS18B20
  * @retval 1=检测到，0=没有
  */
uint8_t Ds18b20Mode_Init(void)
{
    OneWire_Init();
    return OneWire_Reset();
}

/**
  * @brief  启动一次 12 位转换并读取温度
  * @note   转换时间约 750ms（12 位）；读 9 字节暂存器并校验 CRC
  */
uint8_t Ds18b20Mode_Read(float *temp)
{
    uint8_t sp[9];
    uint8_t i;
    int16_t raw;

    if (!OneWire_Reset()) return 0;

    OneWire_WriteByte(DS18B20_CMD_SKIP_ROM);
    OneWire_WriteByte(DS18B20_CMD_CONVERT_T);
    Delay_ms(750);

    if (!OneWire_Reset()) return 0;

    OneWire_WriteByte(DS18B20_CMD_SKIP_ROM);
    OneWire_WriteByte(DS18B20_CMD_READ_SCRATCHPAD);

    for (i = 0; i < 9; i++)
    {
        sp[i] = OneWire_ReadByte();
    }

    if (OneWire_Crc8(sp, 8) != sp[8]) return 0;

    raw = (int16_t)(((uint16_t)sp[1] << 8) | sp[0]);
    *temp = (float)raw * 0.0625f;

    return 1;
}

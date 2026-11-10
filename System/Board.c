#include "Board.h"
#include "stm32f10x.h"
#include "Power.h"
#include "Delay.h"
#include "SoftI2C.h"
#include "OLED.h"
#include "Serial.h"
#include "Serial3.h"
#include "W25Q64.h"
#include "Config.h"

/* 调试：上电扫描 I2C 总线，确认 OLED/传感器地址；确认后改成 0 */
#define I2C_SCAN_ON_BOOT  1

/**
  * @brief  板级初始化（顺序有依赖，别随意调整）
  * @note   1. 电源脚先就位（默认全关）
  *         2. 给 OLED + 传感器上电，等电源稳定
  *         3. 总线和屏幕最先初始化，后面才能显示其他外设状态
  *         4. Flash + 配置
  */
void Board_Init(void)
{
    Power_Rail_Init();

    Power_Sensor_ON();
    Delay_ms(100);

    SoftI2C_Init();
    OLED_Init();
    Serial_Init();
    Serial3_Init();

#if I2C_SCAN_ON_BOOT
    Serial3_SendString("I2C:");
    for (uint8_t addr = 0x08; addr < 0x78; addr++)
    {
        if (SoftI2C_Probe(addr))
            Serial3_Printf(" %02X", (unsigned int)addr);
    }
    Serial3_SendString("\r\n");
#endif

    W25Q64_Init();
    Config_Init();
}

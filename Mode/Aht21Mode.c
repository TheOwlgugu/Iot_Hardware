#include "Aht21Mode.h"
#include "SoftI2C.h"
#include "Delay.h"

#define AHT21_CMD_INIT      0xBE
#define AHT21_CMD_TRIGGER   0xAC
#define AHT21_CMD_RESET     0xBA

/**
  * @brief  初始化/校准 AHT21
  * @note   上电后先等至少 40ms（main 里已经等了 100ms）
  *         状态字节 bit3 = 校准标志，为 0 时再发一次初始化命令
  */
uint8_t Aht21Mode_Init(void)
{
    uint8_t cmd[3] = { AHT21_CMD_INIT, 0x08, 0x00 };
    uint8_t reset  = AHT21_CMD_RESET;
    uint8_t status;

    if (!SoftI2C_IsReady(AHT21_I2C_ADDR)) return 0;

    /* 软复位，清掉上一次上电/断电留下的状态；失败也继续尝试 init */
    SoftI2C_Write(AHT21_I2C_ADDR, &reset, 1);
    Delay_ms(20);

    if (!SoftI2C_Write(AHT21_I2C_ADDR, cmd, 3)) return 0;
    Delay_ms(10);

    if (!SoftI2C_Read(AHT21_I2C_ADDR, &status, 1)) return 0;

    if ((status & 0x08) == 0)
    {
        if (!SoftI2C_Write(AHT21_I2C_ADDR, cmd, 3)) return 0;
        Delay_ms(10);

        if (!SoftI2C_Read(AHT21_I2C_ADDR, &status, 1)) return 0;
        if ((status & 0x08) == 0) return 0;   /* 仍未校准 */
    }

    return 1;
}

/**
  * @brief  触发一次测量并读取温湿度
  * @note   0xAC 0x33 0x00 触发，等 80ms，读 6 字节
  *         湿度 20 位、温度 20 位，换算公式见 AHT20/AHT21 手册
  */
uint8_t Aht21Mode_Read(float *temp, float *humi)
{
    uint8_t cmd[3] = { AHT21_CMD_TRIGGER, 0x33, 0x00 };
    uint8_t buf[6];
    uint32_t raw_h, raw_t;
    uint8_t retry;

    if (!SoftI2C_Write(AHT21_I2C_ADDR, cmd, 3)) return 0;

    /* 等转换完成；如果 status bit7 还是 busy，最多重试 3 次 */
    for (retry = 0; retry < 3; retry++)
    {
        Delay_ms(80);
        if (!SoftI2C_Read(AHT21_I2C_ADDR, buf, 6)) return 0;
        if ((buf[0] & 0x80) == 0) break;
    }

    if (retry == 3) return 0;

    raw_h = ((uint32_t)buf[1] << 16) | ((uint32_t)buf[2] << 8) | buf[3];
    raw_h >>= 4;

    raw_t = (((uint32_t)(buf[3] & 0x0F)) << 16) |
            ((uint32_t)buf[4] << 8) | buf[5];

    /* 全零说明传感器没真正测量（常见于 VCC 没供上），丢弃 */
    if (raw_h == 0 && raw_t == 0) return 0;

    *humi = (float)raw_h * 100.0f / 1048576.0f;
    *temp = (float)raw_t * 200.0f / 1048576.0f - 50.0f;

    return 1;
}

#include "Packet.h"
#include <stdio.h>
#include <string.h>

/**
  * @brief  把一条读数编码成 10 位 ASCII 帧
  * @param  reading    待发送读数
  * @param  device_id  3 位设备编号字符串
  * @param  buf        输出缓冲区
  * @param  size       缓冲区大小（WiFi 至少 12，LoRa 至少 11）
  * @param  newline    1=追加 '\n'，0=不追加
  * @retval 1=成功，0=参数/范围错误
  */
uint8_t Packet_Build(const Reading_t *reading,
                     const char *device_id,
                     char *buf,
                     uint16_t size,
                     uint8_t newline)
{
    int len;

    if (reading == 0 || device_id == 0 || buf == 0) return 0;
    if (!reading->valid) return 0;
    if (reading->type > 99) return 0;
    if (strlen(device_id) != 3) return 0;
    if (reading->value < -9999 || reading->value > 99999) return 0;

    if (size < (uint16_t)(newline ? 12 : 11)) return 0;

    len = snprintf(buf, size, "%s%02u%05d",
                   device_id,
                   (unsigned int)reading->type,
                   (int)reading->value);

    /* 固定 3 + 2 + 5 = 10 个字符；长度不对说明值溢出 */
    if (len != 10) return 0;

    if (newline)
    {
        buf[10] = '\n';
        buf[11] = '\0';
    }

    return 1;
}

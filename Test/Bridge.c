#include "Bridge.h"
#include "Serial.h"
#include "Serial3.h"
#include "Power.h"
#include "Delay.h"

/**
  * @brief  USART3 <-> USART1 直通，按 ESC(0x1B) 退出
  * @param  baud  USART1 使用的波特率（RA-08 侧）
  */
void Bridge_Run(uint32_t baud)
{
    if (baud < 1200 || baud > 921600) baud = 9600;

    /* 配置模式下 LoRa 电源还没打开，先上电 */
    Power_Lora_ON();
    Delay_ms(300);

    Serial_SetBaudRate(baud);
    Serial3_Printf("\r\n[Bridge] USART3 <-> RA-08 @ %lu baud, ESC to exit\r\n",
                   (unsigned long)baud);

    while (1)
    {
        if (Serial3_Available())
        {
            uint8_t c = Serial3_GetByte();
            if (c == 0x1B) break;
            Serial_SendByte(c);
        }

        if (Serial_Available())
        {
            uint8_t c = Serial_GetByte();
            Serial3_SendByte(c);
        }
    }

    Serial_SetBaudRate(115200);
    Power_Lora_OFF();
    Serial3_SendString("\r\n[Bridge] exit\r\n");
}

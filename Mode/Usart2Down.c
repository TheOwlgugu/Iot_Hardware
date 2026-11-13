#include "Usart2Down.h"
#include "Serial2.h"
#include <stdio.h>

/**
  * @brief  初始化 USART2 为 9600（和 PMS7003 一致）
  */
void Usart2Down_Init(void)
{
    Serial2_Init(9600);
    Serial2_DisableRx();   /* 只发不收，防止 RX 噪声唤醒 WFI */
}

/**
  * @brief  把 5 位 code 以 ASCII 发到 USART2
  * @param  code 0~99999
  * @retval 1=已发送，0=超范围
  * @note   5 位 code，暂不加换行
  */
uint8_t Usart2Down_Command(uint32_t code)
{
    char buf[8];

    if (code > 99999) return 0;

    /* 换行先不加；需要时改成 "%05lu\r\n" */
	snprintf(buf, sizeof(buf), "%05lu\r\n", (unsigned long)code);
    //snprintf(buf, sizeof(buf), "%05lu", (unsigned long)code);
    Serial2_SendString(buf);

    return 1;
}

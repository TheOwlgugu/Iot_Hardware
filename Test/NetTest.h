#ifndef __NETTEST_H
#define __NETTEST_H

#include "stm32f10x.h"

/*
 * 网络测试模式：公共大循环 + 具体网络驱动
 * 具体网络（WiFi/LoRa）由 WiFiTest.c / LoRaTest.c 提供，互不依赖。
 * 进入后不返回，靠断电重启回到正常工作模式。
 */
typedef struct
{
    const char *name;                       /* "WiFi" / "LoRa" */
    uint8_t     is_lora;                    /* 1 = LoRa（决定占空比门限） */
    uint8_t   (*init)(void);                /* 外设初始化，1=成功 */
    uint8_t   (*connect)(void);             /* 连接网络，1=成功 */
    uint8_t   (*send)(const char *frame);   /* 发一帧 10 字节 */
    void      (*poll)(void);                /* 非阻塞处理下行 */
} NetTestOps;

void NetTest_Run(const NetTestOps *ops);

#endif

#ifndef __NETWORK_H
#define __NETWORK_H

#include "stm32f10x.h"
#include "Packet.h"

/* 网络类型枚举 */
#define NETWORK_HEARTBEAT_SEC  60   /* 纯执行器节点的心跳周期（秒，LoRa） */
#define NETWORK_WIFI_HEARTBEAT_SEC 30  /* WiFi 心跳：短于热点 NAT 空闲超时 */

#define NET_WIFI  0
#define NET_LORA  1

/* 全局网络类型（由 ConnectMode 设置） */
extern uint8_t g_connType;

/**
  * @brief  连接网络（根据 g_connType 自动分发）
  * @retval 1=成功，0=失败
  */
uint8_t Network_Connect(void);

/* 按指定网络类型连接（测试模式用，不依赖 g_nodeMode） */
uint8_t Network_ConnectType(uint8_t type);

/**
  * @brief  发送数据报（根据 g_connType 自动分发）
  * @param  msg  以 '\0' 结尾的数据报字符串
  * @retval 1=成功，0=失败
  */
uint8_t Network_Send(const char *msg);

/**
  * @brief  发送一条传感器读数（内部调用 Packet_Build）
  * @param  reading  待发送读数
  * @retval 1=成功，0=失败
  */
uint8_t Network_SendReading(const Reading_t *reading);

/* 链路是否已连上 */
uint8_t Network_IsUp(void);

/* 是否已经成功发出过至少一帧（看门狗启动条件） */
uint8_t Network_HasSent(void);

/* 发送一帧心跳（类型 99），用于注册路由/保活 */
uint8_t Network_SendHello(void);

/* 主循环调用：心跳 + 链路状态刷新 */
void Network_Tick(void);

/* 链路维护：主循环每轮调用，断了按退避重连 */
void Network_Poll(void);

/* 打印链路统计 */
void Network_PrintStats(void);

#endif

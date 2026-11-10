#ifndef __BOARD_H
#define __BOARD_H

/*
 * 板级初始化：电源轨、I2C/OLED、串口、Flash、配置
 * main 只需要调用一次 Board_Init()，不用关心外设初始化顺序
 */
void Board_Init(void);

#endif

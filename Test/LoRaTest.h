#ifndef __LORATEST_H
#define __LORATEST_H

#include "NetTest.h"

/* LoRa 测试驱动：独立于 Network/g_nodeMode，只用 RA-08 驱动 + config */
const NetTestOps *LoRaTest_Ops(void);

#endif

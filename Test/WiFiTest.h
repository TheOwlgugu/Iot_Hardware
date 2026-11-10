#ifndef __WIFITEST_H
#define __WIFITEST_H

#include "NetTest.h"

/* WiFi 测试驱动：独立于 Network/g_nodeMode，只用 ESP8266 驱动 + config */
const NetTestOps *WiFiTest_Ops(void);

#endif

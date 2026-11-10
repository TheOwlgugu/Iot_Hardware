#include "TestMode.h"
#include "NetTest.h"
#include "WiFiTest.h"
#include "LoRaTest.h"
#include "Bridge.h"
#include "Network.h"
#include <string.h>
#include <stdlib.h>

uint8_t TestMode_Choose(const char *cmd)
{
    if (cmd == 0) return 0;

    if (strncmp(cmd, "test:net=0", 10) == 0) { NetTest_Run(WiFiTest_Ops()); return 1; }
    if (strncmp(cmd, "test:net=1", 10) == 0) { NetTest_Run(LoRaTest_Ops()); return 1; }
    if (strncmp(cmd, "test:bridge=", 12) == 0) { Bridge_Run((uint32_t)atol(cmd + 12)); return 1; }

    return 0;
}

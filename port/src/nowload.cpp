#include "common.h"

#include "nowload.hpp"
#include "test_hooks.hpp"

extern NowLoadingInfo PortLoadInfo asm("NowLoad_LoadInfo");
extern int PortProgBarCnt asm("NowLoad_ProgBarCnt");
extern float PortProgBarWidthStep asm("NowLoad_ProgBarWidthStep");

PORT_OVERRIDE void NowLoadingBarSteEnd() {
    PortProgBarCnt = PortLoadInfo.step_count;
    PortProgBarWidthStep = GameFastLoad() ? 1.0f : 0.05f;
}

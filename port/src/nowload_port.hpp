#pragma once
#include "nowload.hpp"

// Retail loading-bar state shared by the native loading hook and its callers.
extern NowLoadingInfo PortLoadInfo asm("NowLoad_LoadInfo");
extern int            PortProgBarCnt asm("NowLoad_ProgBarCnt");
extern float          PortProgBarWidthStep asm("NowLoad_ProgBarWidthStep");

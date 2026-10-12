#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>

#include "runscript.hpp"
#include "villagermngr.hpp"

TEST(Records, CallDataPairFitsItsAllocation) {
    EXPECT_GE(sizeof(RS_CALLDATA) * 2 / 16 * 16, sizeof(RS_CALLDATA) * 2);
}

TEST(Records, ProgressInfoPlaceTableLayout) {
    CVillagerPlace::ProgressInfo info{};

    EXPECT_EQ(offsetof(CVillagerPlace::ProgressInfo, place), 8u);
    EXPECT_EQ(
        reinterpret_cast<std::uintptr_t>(&info.place[1][1]) - reinterpret_cast<std::uintptr_t>(&info.place[0][0]),
        sizeof(void *) * 3);
}

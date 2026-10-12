#include <gtest/gtest.h>

#include <cstdint>
#include <cstdlib>

#include "mg_frame.hpp"

TEST(MgFramePositionDeathTest, WorldPositionAcceptsEightByteAlignedOutput) {
    EXPECT_EXIT(([] {
        mgCFrame frame;
        frame.SetPosition(3, 5, 7);
        alignas(16) float storage[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
        float *out = storage + 2;
        if (reinterpret_cast<std::uintptr_t>(out) % 16 != 8) {
            std::exit(2);
        }
        frame.GetWorldPosition0(out);
        if (out[0] != 3 || out[1] != 5 || out[2] != 7 || out[3] != 1 ||
            storage[0] != -1 || storage[1] != -1 || storage[6] != -1 || storage[7] != -1) {
            std::exit(3);
        }
        std::exit(0);
    }()), testing::ExitedWithCode(0), "");
}

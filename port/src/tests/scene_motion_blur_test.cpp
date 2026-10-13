#include <gtest/gtest.h>

#include <cstddef>
#include <cstring>

#include "event.hpp"
#include "event_func.hpp"
#include "mg_memory.hpp"
#include "mg_texture.hpp"
#include "runscript.hpp"
#include "scenesnd.hpp"

namespace {
struct BlurProgram {
    RS_PROG_HEADER header{};
    RS_PROGDATA entry{100, offsetof(BlurProgram, function)};
    funcdata function{};
    vmcode_t code[4]{{RS_OP_PUSH_CONST, RS_CONST_INT, EVENT_EXT_SET_MOTION_BLUR},
                     {RS_OP_PUSH_CONST, RS_CONST_INT, 100},
                     {RS_OP_EXT, 2, 0}, {RS_OP_END, 0, 0}};

    BlurProgram() {
        std::memcpy(header.magic, "SB2", 3);
        header.main = offsetof(BlurProgram, function);
        header.code = offsetof(BlurProgram, code);
        header.prog = offsetof(BlurProgram, entry);
        header.prog_num = 1;
    }
};
} // namespace

TEST(SceneMotionBlur, AliasUsesTheFadeFieldWithoutChangingTheCaptureTexture) {
    CScene scene;
    mgCTexture capture{};
    scene.fade.cross_texture = &capture;
    scene.motion_blur = 100;
    EXPECT_EQ(scene.fade.blur_alpha, 100);
    EXPECT_EQ(scene.fade.cross_texture, &capture);
    EXPECT_EQ(static_cast<void *>(&scene.motion_blur), static_cast<void *>(&scene.fade.blur_alpha));
}

TEST(SceneMotionBlur, EventExternalPreservesItsExistingCrossfadeTexture) {
    CScene scene;
    mgCTexture capture{};
    scene.fade.cross_texture = &capture;
    alignas(64) u_long128 storage[1024]{};
    mgCMemory memory;
    memory.stSetBuffer(storage, 1024);
    BlurProgram program;
    SetEventScript(reinterpret_cast<char *>(&program), nullptr, &memory);
    EXPECT_EQ(RunEvent(100, &scene), 0);
    EXPECT_EQ(scene.fade.blur_alpha, 100);
    EXPECT_EQ(scene.fade.cross_texture, &capture);
    SetEventScript(nullptr, nullptr, nullptr);
    EventScene = nullptr;
}

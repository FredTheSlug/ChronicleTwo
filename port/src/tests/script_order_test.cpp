#include <gtest/gtest.h>

#include "event_func.hpp"
#include "runscript.hpp"

TEST(ScriptOrder, VoicePackAndClipComeFromDifferentArguments) {
    CRunScript script;
    SetEventFunc(&script);
    RS_STACKDATA args[3] = {};
    args[0].type = RS_INT;
    args[0].val.i = 0;
    args[1].type = RS_STR;
    args[1].val.s = const_cast<char *>("abc123");
    args[2].type = RS_STR;
    args[2].val.s = const_cast<char *>("clip456");
    testing::internal::CaptureStderr();
    script.ext_func_table[EVENT_EXT_STREAM_OPEN](args, 3);
    const std::string output = testing::internal::GetCapturedStderr();
    EXPECT_NE(output.find("Vabc123.VPK;1"), std::string::npos);
    EXPECT_NE(output.find("pack clip456.wav"), std::string::npos);
    EXPECT_EQ(args[1].val.s, std::string("abc123"));
    EXPECT_EQ(args[2].val.s, std::string("clip456"));
}

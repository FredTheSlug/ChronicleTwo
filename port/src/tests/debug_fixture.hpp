#pragma once
#include <gtest/gtest.h>

#include <filesystem>
#include <format>

#include "platform/config.hpp"
#include "platform/paths.hpp"
#include "platform_fixture.hpp"

namespace dc::test {
// Debug routing cases opt in using an isolated config, then restore ordinary defaults.
class DebugFixture : public testing::Test {
protected:
    void SetUp() override {
        root = std::filesystem::temp_directory_path() / std::format("dc2_debug_{}", ProcessId());
        PathsSetSaveRoot(root);
        Config config;
        config.debug_mode = true;
        ASSERT_TRUE(ConfigChange(config));
    }

    void TearDown() override {
        ConfigChange(Config{});
        std::filesystem::remove_all(root);
    }

    std::filesystem::path root;
};
} // namespace dc::test

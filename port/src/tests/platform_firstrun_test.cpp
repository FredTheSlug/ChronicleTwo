#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include "exitcodes.hpp"
#include "platform/firstrun.hpp"
#include "platform/paths.hpp"

namespace fs = std::filesystem;

namespace {

class PlatformFirstRun : public testing::Test {
protected:
    void SetUp() override {
        base_ = fs::temp_directory_path() /
                ("firstrun_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(base_);
    }

    void TearDown() override {
        std::error_code error;
        fs::remove_all(base_, error);
    }

    fs::path Root() const { return base_ / "data"; }

    fs::path base_;
};

} // namespace

TEST_F(PlatformFirstRun, MissingRootIsMissing) {
    EXPECT_TRUE(FirstRunDataMissing(base_ / "absent"));
}

TEST_F(PlatformFirstRun, EmptyDirectoryIsMissing) {
    fs::create_directories(Root());
    EXPECT_TRUE(FirstRunDataMissing(Root()));
}

TEST_F(PlatformFirstRun, ManifestDirectoryIsMissing) {
    fs::create_directories(Root() / "manifest.txt");
    EXPECT_TRUE(FirstRunDataMissing(Root()));
}

TEST_F(PlatformFirstRun, RegularFileRootIsMissing) {
    std::ofstream(Root(), std::ios::binary) << "not a directory";
    EXPECT_TRUE(FirstRunDataMissing(Root()));
}

TEST_F(PlatformFirstRun, ManifestPresentIsNotMissing) {
    fs::create_directories(Root());
    std::ofstream(Root() / "manifest.txt", std::ios::binary) << "";
    EXPECT_FALSE(FirstRunDataMissing(Root()));
}

TEST_F(PlatformFirstRun, MessageNamesRootAndExtractionCommand) {
    const std::string message = FirstRunDataMessage(Root());
    EXPECT_NE(message.find(PathsDisplay(Root())), std::string::npos) << message;
    EXPECT_NE(message.find("dc2data extract"), std::string::npos) << message;
    EXPECT_NE(message.find("DATA.HD4"), std::string::npos) << message;
}

TEST_F(PlatformFirstRun, MissingDataExitsWithNoDataAndNamesTheCommand) {
    EXPECT_EXIT(FirstRunRequireData(base_ / "absent", /*headless=*/true), testing::ExitedWithCode(kExitNoData),
                "dc2data extract");
}

TEST_F(PlatformFirstRun, PresentDataReturns) {
    fs::create_directories(Root());
    std::ofstream(Root() / "manifest.txt", std::ios::binary) << "";
    FirstRunRequireData(Root(), /*headless=*/true);
}

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <vector>

#include "dataread.hpp"
#include "menucapt.hpp"
#include "menumain.hpp"
#include "platform/paths.hpp"
#include "scene.hpp"

TEST(MenuChapter, MissingNarrationTimesOutAndReopeningRestartsFade) {
    const auto root = std::filesystem::temp_directory_path() / "dc2_chapter_test";
    std::filesystem::create_directories(root);
    std::ofstream(root / "manifest.txt");
    PathsConfigure({.app_dir = "dc2-chapter-test", .env_data = "DC2_CHAPTER_TEST_DATA", .env_save = "DC2_CHAPTER_TEST_SAVE"});
    PathsSetDataRoot(root);
    InitCDFile();
    auto scene = std::unique_ptr<CScene, decltype(&std::free)>(
        static_cast<CScene *>(std::calloc(1, sizeof(CScene))), &std::free);
    ASSERT_TRUE(scene);
    auto *saved_scene = MenuMainScene;
    MenuMainScene = scene.get();
    std::vector<u_long128> storage(0x4000);
    mgCMemory              stack;
    stack.stSetBuffer(storage.data(), static_cast<int>(storage.size()));
    int blocks[] = {1, 2};
    MenuChapterInit(&stack, blocks, 0, 0);
    EXPECT_EQ(MenuMainScene->fade.end, 0);
    int finished = 0;
    for (int frame = 0; frame < 2100 && !finished; ++frame) {
        MenuMainScene->fade.FadeStep();
        finished = MenuChapterKey();
    }
    EXPECT_EQ(finished, 1);
    MenuChapterInit(&stack, blocks, 0, 7);
    EXPECT_EQ(MenuChapterKey(), 0);
    EXPECT_EQ(MenuMainScene->fade.end, 0);
    EXPECT_FLOAT_EQ(MenuMainScene->fade.alpha, 128.0f);
    MenuMainScene = saved_scene;
    std::filesystem::remove_all(root);
}

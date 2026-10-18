#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <format>
#include <memory>
#include <vector>

#include "dng_event.hpp"
#include "dng_main.hpp"
#include "effscript.hpp"
#include "mg_memory.hpp"
#include "monster.hpp"
#include "monster_memory.hpp"
#include "platform/config.hpp"
#include "platform/paths.hpp"
#include "platform_fixture.hpp"
#include "runscript.hpp"
#include "scene.hpp"

namespace {

// Loads a floor with game.more_monsters set as given, in an isolated save root.
class MonsterMemory : public testing::TestWithParam<bool> {
protected:
    void SetUp() override {
        root = std::filesystem::temp_directory_path() / std::format("dc2_monster_memory_{}", dc::test::ProcessId());
        PathsSetSaveRoot(root);
        Config config;
        config.more_monsters = GetParam();
        ASSERT_TRUE(ConfigChange(config));
    }

    void TearDown() override {
        ConfigChange(Config{});
        std::filesystem::remove_all(root);
    }

    std::filesystem::path root;
};

TEST_P(MonsterMemory, SingleMonsterLoadInitializesEveryHostArena) {
    // Exercise the scripted loader without loading a reference model (-1 is an empty kind).
    auto                                          manager = std::make_unique<CMonsterMan>();
    auto                                          monsters = std::make_unique<CActiveMonster[]>(MONSTER_ACTIVE_MAX);
    std::unique_ptr<CScene, decltype(&std::free)> scene(
        static_cast<CScene *>(std::calloc(1, sizeof(CScene))), &std::free);
    ASSERT_NE(scene, nullptr);
    scene->chara_num = DNG_CHARA_SLOT_MONSTER_END;
    for (int i = 0; i < MONSTER_ACTIVE_MAX; ++i) {
        scene->chara[DNG_CHARA_SLOT_MONSTER + i].status = 1;
        scene->chara[DNG_CHARA_SLOT_MONSTER + i].chara = &monsters[i];
    }

    constexpr int          kExtraBytes = 128 * (sizeof(RS_STACKDATA) - 8) + 512 * (sizeof(RS_CALLDATA) - 12);
    constexpr int          kSlotQuads = 4000 + (kExtraBytes + 15) / 16;
    std::vector<u_long128> storage(MONSTER_ACTIVE_MAX * (kSlotQuads + 4) + 16);
    mgCMemory              parent;
    mgCMemory              memory;
    parent.stSetBuffer(storage.data(), storage.size());
    scene->stack_num = 4;
    scene->stack[2] = &parent;
    scene->stack[3] = &memory;
    CEffectScriptMan effects;

    CScene           *saved_scene = DngMainScene;
    CMonsterMan      *saved_manager = ActiveMonster;
    CEffectScriptMan *saved_effects = FxScriptMan;
    DngMainScene = scene.get();
    ActiveMonster = manager.get();
    FxScriptMan = &effects;
    LoadMonsterFile(-1, 1);
    DngMainScene = saved_scene;
    ActiveMonster = saved_manager;
    FxScriptMan = saved_effects;

    EXPECT_EQ(manager->scene, scene.get());
    EXPECT_EQ(PortMonsterActiveLimit(), GetParam() ? MONSTER_ACTIVE_MAX : 24);
    // Every slot binds its scene character; only the floor's slots get script memory.
    for (int i = 0; i < MONSTER_ACTIVE_MAX; ++i) {
        EXPECT_EQ(manager->active[i], &monsters[i]) << i;
    }
    u_long128 *previous_end = memory.stack;
    for (int i = 0; i < PortMonsterActiveLimit(); ++i) {
        mgCMemory &slot = manager->memory[i];
        EXPECT_EQ(slot.stack_size, kSlotQuads) << i;
        EXPECT_EQ(slot.stack_used, 0) << i;
        EXPECT_EQ(slot.lock, 0) << i;
        ASSERT_NE(slot.stack, nullptr) << i;
        EXPECT_GE(slot.stack, previous_end) << i;
        EXPECT_LE(slot.stack + kSlotQuads, memory.stack + memory.stack_size) << i;
        EXPECT_EQ(slot.Alloc(38), slot.stack) << i;
        EXPECT_EQ(slot.stack_used, 38) << i;
        previous_end = slot.stack + kSlotQuads;
    }
}

INSTANTIATE_TEST_SUITE_P(MoreMonsters, MonsterMemory, testing::Bool(),
                         [](const testing::TestParamInfo<bool> &info) { return info.param ? "On" : "Off"; });

} // namespace

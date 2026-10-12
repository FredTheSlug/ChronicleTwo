#pragma once

#include <optional>
#include <string_view>
#include <vector>

#include "mainloop.hpp"

enum class JumpMode { Town, Dungeon, Event, Title, Opening, Menu };
struct GameJump {
    JumpMode mode = JumpMode::Menu;
    int map = 10;
    int floor = 1;
    int event = 0;
};

bool GameParseJump(std::string_view text, GameJump &jump);
bool GameParseGive(std::string_view text, std::vector<int> &items);
bool GameSetJump(std::string_view text);
bool GameSetGive(std::string_view text);
bool GameHasJump();
bool GameDebugEnabled();
void GameSetFastLoad(bool fast);
bool GameFastLoad();
void GamePrepareJump(int &mode, INIT_LOOP_ARG &arg);
void GameGiveOnArrival(int mode);
bool GameDeveloperMenuRequested(int mode);
bool GameDebugPauseResumeRequested(CGamePad &pad);

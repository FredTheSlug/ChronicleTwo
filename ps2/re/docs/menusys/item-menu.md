# menusys: item menu commands, places and layouts

Values behind the enums added to `menusys.hpp`, and what they were derived from.

## Item commands (`ITEM_MENU_COMMAND`)
`GetMenuCommandMsg(item_no, cmds)` fills the command list of an item with message numbers; the command is the message
number minus 5000 (`ITEM_MENU_COMMAND_MSG_BASE`). `GetItemCommandMsg` then filters, reorders and greys the list
(colour `0x80202020` = cannot be chosen, `0x80303030` = gift box with room), `CBaseMenuClass::MenuItemCommandSelect`
runs the chosen command and `CMenuItemInfo::ItemCmdAfter` acts on its outcome.

| No. | Name | Evidence |
|---|---|---|
| 1 | THROW_AWAY | ItemCmdAfter opens the throw-away question (`SetPreCmdTrush`); removed unless `IsTrush()` |
| 2 | EQUIP | swaps the item into `IsItemtypeWhoisEquip`'s slot; refused for curse/stop/stone |
| 3, 8 | STATUS, INFO | both open the weapon page / attachment status check / ridepod page |
| 4 | REPAIR | uses the repair powder `GetEnableRepairItemNo()` names on the item |
| 5 | SET_ACTIVE_ITEM | `SearchActiveItemTableSpace`, then a move into the active slot |
| 6 | UNEQUIP | move from an equipment slot to a free bag place; grey without bag space |
| 7 | REMOVE_ACTIVE_ITEM | replaces 5 on the active-item panel; move to a free bag place |
| 9 | SPECTRUMIZE | opens `SetPreCmdSpectolBreak`; removed unless `IsSpectolTrans()` |
| 10 | EQUIP_RIDEPOD_PART | fits a part; grey when `IsEnableChangeRoboParts` fails |
| 11, 12 | OPEN_AQUARIUM, OPEN_MONSTER_BOX | sub menus 0 (`MenuAquaInit`) and 1 (`MenuMonsterBoxInit`) |
| 13 | GIFT_BOX_CONTENTS | `SetPreCmdGiftBoxSelect` |
| 14 | PUT_IN_AQUARIUM | `FishInAquarium`; offered only while item 0x135 is owned |
| 15..18 | USE_ON_MAX / MONICA / RIDEPOD / MONSTER | `human_tbl` per page: Max {15,16}, Monica {16,15}, ridepod {17}, monster {16,15}; 18 replaces 16 on the monster page; 17 does nothing |
| 19, 20 | USE_ON_PRIMARY/SECONDARY_WEAPON | `UseItem` on `equip[cmd - 19]` |
| 21 | REPAIR_RIDEPOD | replaces 19 on the ridepod page; `UseItem` on `robo->parts[0]` |
| 22 | RECHARGE_RIDEPOD | uses ridepod fuel (`ITEM_ID_RIDEPOD_FUEL`); grey when the energy is full |
| 23 | VIEW_BUILD_UP | `NextModeBuildUpInfo`; marked when a build-up is ready |
| 24, 25 | SET_BAIT, SET_LURE | copy the item into the active bait slot (24 is prepended for bait items, attribute bit 4) |
| 26 | RENAME | sub menu 2 (`NameRegistInit`); the ridepod core renames the ridepod |
| 27, 28 | REMOVE_BAIT, REMOVE_LURE | appended for the fishing rod / lure rod while bait is on it |
| 29, 38, 47 | VIEW_QUESTS, VIEW_SCOOP_MEMO, OPEN_MONSTER_BOOK | sub menus 3, 4 (`MenuNPCQuestViewInit` 0/1) and 5 (`MonsterBookInit`) |
| 30 | TAKE_ONE | swaps one of the item onto the cursor |
| 32, 33 | TAKE_BAIT, TAKE_LURE | handled like 27/28 |
| 34, 35 | ENABLE/DISABLE_RIDEPOD_VOICE | `SetRoboVoiceFlag` |
| 37 | USE_ON_CORE | `UseItem` on the ridepod core; script "core NOT UP" when nothing happened |
| 42 | BOIL_FISH | `Boiled()`; offered only while item 0x166 is owned |
| 43 | ESCAPE_DUNGEON | asks (script "escape?"), then uses the item up and closes the menu; grey in town |
| 44 | BREAK_SEAL | clears `DNG_FLOOR_SEAL_MASK`; grey unless the floor is sealed |
| 45 | SHOW_FISH_CODE | shows `EncodePassword` of the fish; offered once Finny Frenzy is unlocked (message 0x13B5) |
| 46 | VIEW_FISHING_ROD | enters `MENU_ASK_MODE_ROD_POINTS` |

## Outcome of a command (`ITEMCMD_RET_PARA`)
- `se_no`: the sound ItemCmdAfter plays first (`MenuSePlay(ret->se_no)`): `SYSTEM_SE_DECIDE`, `SYSTEM_SE_CANCEL`, 6, 8
  (equip) or 0x1C (refused); -1 for none.
- `menu_cmd`: the chosen `ITEM_MENU_COMMAND`, -1 when cancelled, -2 before a choice.
- `detail`: why equipping failed (1 curse, 2 stop, 3 stone: index into `exename`), 10 when an active-item stack is split,
  or the item number used by the use-on commands.
- `no_equipped_rod`: set by the bait commands when the rod is not equipped (`NowFishingStyle() == 0`).

## Places (`MENU_SWAP_TYPE`) and move tables
`MENU_SWAPITEM_INFO::type`, `CBaseMenuClass::cmd_arg_pos`, `MenuItemCmdArgPos` and the `place` argument of
`GetItemCommandMsg` share one numbering: 0 active item, 1 equipment, 2 ridepod part, 3 item board, 4 invention-menu
list (`inventmn.cpp` uses `Set(4, ...)`), 7 the weapon of the weapon page, 9 unknown (handled like the item board),
10 the bait slot. `SetItemCmdMsgPos` also handles 5, 6 and 8, which no menusys caller passes.

`CommonSetMoveItemClass` takes two rows of `int[4]`: place kind (`MENU_MOVE_PLACE`), character, list
(`MENU_MOVE_LIST`) and slot. Kind 3 (`TEMP`) is the split-off `MenuMoveTempGameDataUsed`, drawn as if from the bag.

`CMenuKeyFunc::EnableSwapNowPos` returns a `MENU_SWAP_CHECK`; PushKey plays the cancel sound for 1, 2, 8 and 9, asks how
many for 4, uses the item for 5 and plays 0x1C for 3.

## Layouts (`ITEM_MENU_PANEL`) and pages
`item_menu_argtbl` has one row per panel. Steps of +-101..104 always leave the range, so with `MENU_CURSOR_LIMIT_EXIT`
they jump to `exit_no`. The initial panel per character page is `menuitem_initmenumode` = {CHARA, CHARA, RIDEPOD,
MONSTER}. Screens opened from the item menu are `MENU_ITEM_SUB_MENU` 0..5 (see the table above).

`CMenuItemInfo` adds two ask modes: 13 (`MENU_ASK_MODE_ITEM_OVER`, the over-limit message) and 15
(`MENU_ASK_MODE_ROD_POINTS`, spending a fishing rod's points 100 per level up to 100, steps `MENU_ROD_POINTS_STEP`).

## Shift-JIS script names
ItemCmdAfter / IsAskExtend / PushKey run menu scripts by Japanese name: capacity insufficient, part broken, defence up /
not up, thirst, core not up, escape?, fish password, seal release?, build up? / NG / finished, pre-exit processing.
The comments next to each call give the meaning.

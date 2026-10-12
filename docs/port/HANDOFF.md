# Handoff: Dark Chronicle PC port after Phases 4-6

Written at the Phases 4-6 push point (Wave 3 end, 12 Oct 2026). Read this first, then `docs/PC.md` (what is built), `docs/PC_PORT_PLAN.md` (the plan and phase status) and `docs/port/process/` (the schedule this work followed, its addenda and the wave records).

## 1. State of the three repositories

| Repository | Branch | Tip at push | Remote |
|---|---|---|---|
| ChronicleTwo (`C:\Users\Adubbz\Development\PS2\Projects\ChronicleTwo`) | `port/pc`, pushed as `master` | the `Docs:` commit on top of `1ac1cd1` | origin/master |
| Kazarov (`...\Kazarov`) | `main` | `f1b8e80` | TheMoonPeople/Kazarov `main` |
| Chronicle (`...\Chronicle-phase0`, a worktree of `Chronicle` on `master`) | `master` | `c6a8c918` (pin `main@f1b8e80`) on `cd17ef91` (arenas) on upstream `109c6eff` | origin/master |

Both games pin Kazarov `f1b8e80`. `C:\Users\Adubbz\Development\PS2\Projects\Chronicle` (branch `shared-port`) is someone else's work in progress: never open, build or run git there. Chronicle is built only in `Chronicle-phase0`.

Upstream Chronicle moved during Wave 3 (`88fd2511` .. `109c6eff`, four commits by other contributors in `port/src` and `docs/PC.md`). The two local Chronicle commits were rebased onto it and the Chronicle gate re-run green before the push.

### What Wave 3 merged (ChronicleTwo `fee9c9c..master`)

In order, each stream rebased onto the growing `port/pc`, with its `Kazarov: pin kz/...` commit dropped or stripped of the submodule change:

1. W2.u, `s/uptr`: `135ed08 6bb7a39 1271288 59ee848` (fast-forward). Addresses kept in integers are `uptr`/`sptr` in `ps2/src`; `truncations.py --check`; `docs/port/records.md`.
2. `s/present-core`: `4e62eff 6514f21 d002c5d 3604945`. Ticks as display lists, display frames, the window aspect, the video settings.
3. `s/draw2d`: `4e5ab38 3c507d0 94dd1a8 56e39bf d764543 ca5f059`. mgCDrawPrim, sprites, TexAnime, menu grab, PrintDirect; no GIF decoder. `3c507d0` is draw2d's pin commit reduced to its CMake line (`--long-is-64`).
4. `s/prog0`: `1b1f6e4 74f3d06 7f0bcf8`. MDT visuals as GPU meshes; `1b1f6e4` is prog0's pin commit reduced to its test changes. Its copies of present-core/draw2d commits were dropped (`rebase --onto`).
5. `s/fx-shadow`: `1616b39 8616c87 e78bd4b` (its copy of present-core's seam, `e5a2266`, dropped).
6. `s/audio-game`: `15f61b7 8cea565` (pin dropped).
7. `s/movies`: `65bc53c b646ab5`.
8. Integrator: `e4b4f93` (pin `main@f1b8e80`), `5df989e` (title fixes), `6f47570` (prog0 seam entries, `prog0_meshes`, VIF plumbing retired, widening-only overrides deleted, audio stop at exit), `1ac1cd1` (native gate, FTOI4 to the oracles, 30 s GPU cases), then the `Docs:` commit (PC.md, plan, truncations.md, this file, `docs/port/process/`).

Conflicts were only the uptr one-token lines (`CreatePacket` return types `u_int` -> `uptr` in `mg_sprite.cpp`, `mg_visual.cpp`, `mg_shadow.cpp`, `water.cpp`), resolved toward `uptr`. `mglib.cpp` (present-core, draw2d, prog0 and fx-shadow all touched it) merged automatically and builds.

### What Kazarov merged (`5314543..f1b8e80`)

`a27ddbe` (draw2d: `port_override.py --long-is-64`), `db8e19e` (prog0: Vu_prog0's constants in a set 2 block, `MeshConstants` back to 400 bytes, bone palette per draw), `0b87ccb` (audio-game: stream channel, bank select by CC 0, HS pitch, per-core volume), `f1b8e80` (integrator: `gfx_validation_test.cpp`'s Linux-only message, the `-Wall` warning on Windows). The Chronicle gate ran after each of the three stream merges and on the final tip.

## 2. Gates at the push

- ChronicleTwo Linux (container, main checkout with real data): build ok (only the vendored `stb_*` unused-function warnings, present since the W0 baseline, and `ps2/src` copies); ctest 774 cases, all pass (`DataRead.RealDataIndexesTheDiscFiles` timed out once under load and passed on rerun).
- ChronicleTwo Windows: build ok (no duplicate or undefined symbol lines); tests 780/780 on the RTX 4090; real-data headless runs show the language select (300 frames) and the title (2500 frames).
- `ps2-pal`: `SCES_511.90: OK`, every section.
- `truncations.py --build port/build/pc --check`: clean; 47 rows, 15 narrowing sites in compiled `ps2/` code, all owned, 0 open round trips in linked code. `docs/port/truncations.md` regenerated.
- `native_gate.py --binary port/build/pc/darkchronicle`: clean (also a CI step now).
- `Ps2Unimplemented` callers: 14 (9 game: `sgLoopGyoRace`, `MenuChapterInit`, `CLockOnModel::Draw`, `CRedMarkModel::Draw`, `CMenuItemUse::UseItem`, four `CGamePad` capture functions; 5 SDK: `sceCdSearchFile`, `sceCdSeek`, `sceMcRename`, `scePadEnd`, `scePadPortClose`).
- Chronicle on `f1b8e80`: Linux ctest 352/352 (incl. the new audio tests), headless 3-frame screenshot byte-identical to the W0 baseline, `ps2-pal` 100% (3442 functions), Windows build ok.
- Kazarov pytest (container): 93 passed, 14 skipped.
- In game, real data: `--frames 300` = language select; `--input` past it: Level-5 logo movie frames (frame 540), the title over its 3D map (frame 2500), `--display-per-tick 4` byte-identical at the title, `DC2_AUDIO_WAV` non-silent at the title, the attract demo's opening event (frame 4500, skinned characters).

Not run at this push (W3 exit-gate items left for W4 `present`): the 2560x1440 windowed run's FPS/TICK log, `interpolation = off` in game, the 1024x768 / 1280x720 / 800x1200 aspect screenshots compared by PSNR, the opening and first town by `--input`.

## 3. Where the game stops (the W4 starting point)

Pressing START at the title (frames 2400-2500 of `docs/port/process`'s title script plus a `start` line) makes the game enter the next mode (`REVERB 4 32` logged), then the run stops advancing frames: no crash, no further `mglib: frame` output, the process keeps running. Without START the title loops through its attract demo indefinitely. Debug with gdb in the container: `tools/kazarov/bin/kazarov shell bash -c "gdb -batch -ex run -ex bt --args port/build/pc/darkchronicle --headless --offscreen --data data --save port/build/pc/save --input <script> --frames N"` and interrupt, or add `--frames` just past the press and read where the main loop waits.

The title input script used (frames are game frames; a bare frame number releases everything):

```
300 circle
306
320 cross
326
900 start
906
1200 circle
1206
1500 cross
1506
```

## 4. Open NEEDS and carry-forward (from `docs/port/process/W3-NEEDS.md`, with what happened)

Applied at W3 integration:
- `renderer.prog0_meshes = true` in `main.cpp`; seam entries `mgPortDrawMeshProg0`/`mgPortDrawMeshImmediateProg0`, prog0's `SubmitFaces` uses them.
- `--long-is-64` CMake line taken; the GPU cases (`GfxMeshDc2.*`, `MgTextureGpu.*`, `MgTanimeGpu.*`, `GfxMisc.PipelineCache`) get 30 s.
- PC.md stub table cut to what is linked; `SetMaterialRef` returns 0 noted.
- Widening-only overrides deleted: `mgCMemory::ClearHeapMem`, `EndStackMode`, `stAlloc64`, `StartReadBG`. Kept on purpose: `stAllocTest` (64-bit overflow check), `mgSetPacketBuffer`/`mgSetDataBuffer`/`mgGetFrameBuffer`/`mgSetUserVuProg`/`mgSetUserVuProgAdr` (retail keeps that state in `ps2/src/mglib.cpp` statics the port does not read, so they are state-ownership overrides, not widenings), `mgSetPkFrameBuffer` (registered targets), `SetTableBuffer`/`Initialize` (strides).
- Native-VU audit: no GIF/PACKED decoder, no XYZF2/12.4/FTOI4 packer, no VU1 layout helper linked; `port/src/vu1/` float-only (`Ftoi4` moved to `tests/vu1_oracle/vu1_pack.hpp`); `scripts/port/native_gate.py` is the CI gate. `vu0::Ftoi4` stays in `mg_math.cpp`/`mg_drawprim_asm.cpp`: those are retail VU0 macro functions whose integers the game reads (audit class a).
- Audit #13: the VIF packet arenas and the direct-draw cursor retired (one sentinel `sceVif1Packet`, one never-written direct tag); the data buffers stay.
- prog0's `GetLight`/`GetCharaLight` stubs gone (header-mirror copy of `CFuncPoint`); `DrawSky` stays prog0's port.
- `gfx_validation_test.cpp` warning fixed in Kazarov.
- `mgCVisualMDT::Draw` still calls the virtual `CreateRenderInfoPacket` then `CreatePacket` (shadows and water draw from there): verified.

Deferred, with owner:
- W4 `present`: `mgSetPkMoveImage` frame copies (`fire_work` in `sceneevent.cpp`, `screen` in `map.cpp`) as frame targets (heat haze at wide aspect); audit #14 render targets keyed by handle instead of TBP0; bone-palette interpolation; HUD anchors; display frames replay the tick's prog0 lighting; `mgPortInterpScope` restore resets the outer teleport/blend (no gfx getters); the W3 display/aspect gates not run (above); `GfxMisc.PipelineCache` Windows slowness after the FFmpeg DLLs (now 30 s).
- W4 `sweep-town`: `gyorace.cpp` `sgEffectDrawGyoRace` with `sgLoopGyoRace` (shared unit state; use `mgPortPrimVertex`/`mgPortPrimSubmit`); `MenuChapterInit`, `CMenuItemUse::UseItem`.
- W4 `sweep-dungeon` or `records`: `event_func` `_SWE_SET_COLOR`/`_SWE_SET_TEXTURE`/`_SWE_START_EFFECT` `(slot << 2)` strides; `CRedMarkModel::Draw`, `CLockOnModel::Draw`.
- `records` (running from `59ee848` when this was written; it owns `docs/port/records.md`, `scripts/port/records.py`, `runscript_port.*`, `runscript_opcodes.cpp`, `vlgr_info.cpp`, `villagermngr.cpp`, `editdata.cpp`, `editriver.cpp`, `effscript.cpp`, `records_test.cpp`, PC.md "Records"): villager `GetAppearVlgr` stride, `_SET_INDEXOBJ_SIZE`/`_GET_INDEXOBJ_SIZE`, `CMenuSystemData::item_cursor`, `DATA_HEADER` fixup gate, `funcdata::name`, `USER_PICTURE_INFO`/`CInventUserData` image fixup. It must rebase onto the pushed `master`; `docs/port/truncations.md` was regenerated here, so expect to regenerate it again. Merged after W3; see "Stream `records` (W4)" below.
- arena/W4: `effscript.cpp:5564` `RS_CALLDATA` one quadword for call depth 2 (undersized in retail too).
- Open (draw2d): per-object host tables grow with distinct object addresses (3D sprite records never erased); bound them per mode.
- Open (prog0): GPU meshes for Fix/Motion faces are never freed (live until the face address is reused).
- Open (fx-shadow): `mgStoreZBuffImage` answers one tick late (photo's first distance is 0); no shadow/water/outline/DOF/lens-flare in-game screenshot yet (the title and demo do not show them clearly).
- Open (audio-game): end-of-stream 0x8000 held 1 s; `SndInReverb` no-op; movie PCM on mixer port 5 (`kMoviePortAudioPort`) rather than a stream channel, documented in PC.md "Movies".
- Open: "gfx: a draw samples the target it renders to; snapshot it first" is logged during the logo sequence (a draw binds the target it renders into); harmless so far, find the caller.
- Fixed after W3: `StrideFix.DrawManagerTablesHoldPointers` segfaulted intermittently because the test's stack `mgCDrawManager` left `render_info` unset (the retail constructor does not set it; the game's instance is a zero-initialised global) and `SetSortTable` read it. The test now clears it; the draw manager overrides were right. `ctest --repeat until-fail:20 -R StrideFix` is green.
- Open: `MouseCapture.CapturesWhenFullscreenStartupGainsFocusLater` (Kazarov) fails intermittently on Windows while other `darkchronicle.exe` windows are open on the desktop (they take its focus); it passes on rerun. No Kazarov change touched it.
- Open: Chronicle's Windows `--headless` aborts with `filesystem_error ... __wide_to_char` after exporting the game text (predates Phase 0; for the Chronicle owner).

## 5. The W4 plan

`docs/port/process/SCHEDULE.md` section 5 "Wave 4" and the briefs in section 9 (`present`, `options`, `sweep-town`, `sweep-dungeon`, W4 integrator). Add to the W4 briefs: the deferred lines above, the hang after START at the title as the sweeps' first target, the W3 display/aspect gates that were not run, and that `records` finishes first or in parallel with its own ownership.

## 6. How to resume

- Worktrees and branches: `docs/port/process/SCHEDULE.md` section 4.1 (`ChronicleTwo-<stream>` on `s/<stream>` from `port/pc`; `Kazarov-<stream>` on `kz/<stream>` from `main`; data and `.kazarov\windows` as junctions; `rom/pal/` with the ISO hardlinked for ps2-pal, never a `rom` junction). Remove a worktree only after `cmd /c rmdir <wt>\data` and `cmd /c rmdir <wt>\.kazarov\windows`: `git worktree remove --force` follows junctions and once deleted the main `data/`.
- Pinning a stream's Kazarov commit: section 4.2. The integrator rewrites pins to `main` and strips the submodule change from stream pin commits.
- Gate commands: section 4.4, plus at every gate `kazarov shell python3 scripts/port/truncations.py --build port/build/pc --check`, `kazarov shell python3 scripts/port/native_gate.py --binary port/build/pc/darkchronicle`, and from W3 `records.py` once `records` lands.
- Windows: run `tools/windows/*.ps1` from pwsh 7 with the checkout as the working directory (Windows PowerShell 5.1 aborts on cmake's stderr); `ctest` is `& 'C:\Program Files\CMake\bin\ctest.exe'`; run the Windows test gate with nothing else heavy running (load causes 5 s timeouts); no Vulkan validation layer on Windows.
- Docker: if Docker Desktop will not start, stop a stale `com.docker.extensions` process and restart it. Image tag = hash of `tools/kazarov/image` (currently `kazarov-dev:b4dcf4978934`).
- Linux runs from a worktree see no data (junctions do not resolve in the container): data-dependent Linux checks run in the main checkout.
- Kazarov pytest only in the container: `tools/kazarov/bin/kazarov shell bash -c "python3 -m venv /tmp/v && /tmp/v/bin/pip -q install pytest && cd tools/kazarov && /tmp/v/bin/python -m pytest tests -q"`.
- Saves: `--save port/build/<config>/save`, never a `save/` beside the executable.
- Git policy: author `Adubbz <Adubbz@users.noreply.github.com>`; commit messages `Port:`/`Kazarov:`/`Build:`/`Docs:` with no co-author or tool attribution; pushes fast-forward only, in the order Kazarov `main`, Chronicle `master` (from `Chronicle-phase0`), ChronicleTwo `port/pc:master`, and every pin must exist on Kazarov `main` first. Check upstream Chronicle before pushing it: others push there.

## Stream `records` (W4)

Merged into `port/pc` and pushed to `master` (rebased onto the StrideFix test fix; details and the per-format notes in `docs/port/records.md`):

- 57 records carry `PORT_RECORD_ASSERT(sizeof(T) == N)` beside their `STATIC_ASSERT`; all hold on PC, and `scripts/port/records.py --build port/build/pc --require-clang` (ctest `Records.Check`) proves sizes and the absence of pointer, `uptr`/`sptr` and vtable members.
- `funcdata::name` is `u32`: it is zero in every disc script and retail never resolves it, and as `char *` it moved `local`/`arg` to 16/20 on PC. This is the one pointer-typed declaration changed, kept as a documented exception (approved): the field is a 32-bit on-disc word, always 0, never resolved; `docs/PC.md` "Records" states it.
- `CVillagerMngr::GetAppearVlgr` (port/src/villagermngr.cpp) indexes `place[alternative][time]` and the output arrays; retail's 4-byte strides are kept in ps2/src because the index form changed MWCC's code.
- `_GET_INDEXOBJ_SIZE` (port/src/runscript_opcodes.cpp) indexes `entry_object`; retail sums the address into an `int`.
- `SetEffectScript` (ps2/src/effscript.cpp) sizes its call data by `sizeof(RS_CALLDATA) * 2 / 16`, byte-identical on the PS2.
- memcard.cpp sizes the save, album and omake images by sizeof; `runscript.cpp`'s stack dump prints strings through `val.s`.
- The 535 runtime structs were rechecked through their 407 cast sites: no new records.

Left:

- `CMenuItemInfo::ExitEnd` (`ps2/src/menusys.cpp:6860`) reads `MenuCommonInfo->cursor` as a `void *` and truncates it; the plain read is not byte-identical, so it needs an override by the menus' owner. The value is right on little-endian hosts.
- Save images are host layout on PC; `USER_PICTURE_INFO::image` comes back from the card stale until `CInventUserData::ResetAddress` (`ps2/src/inventmn.cpp:843`, called at 6227 and 7079) runs. A reader before menu init would see a stale host address; none was found.
- `mgCVisualFixMDT::DataAssignMDT` (`ps2/src/mg_visual.cpp:610`) stores an address in `mgCFace::packet_tag_word` (`u_int`), a runtime packet field for the mesh owner.
- From `docs/port/HANDOFF.md` section 4, still open and not done by `records`: the `event_func` `_SWE_SET_COLOR`/`_SWE_SET_TEXTURE`/`_SWE_START_EFFECT` `(slot << 2)` strides (now W4 `sweep-dungeon`), the `CMenuSystemData::item_cursor` save field and `_SET_INDEXOBJ_SIZE` (only `_GET_INDEXOBJ_SIZE` is indexed), and the `DATA_HEADER` fixup gate (PS2 only; the host index replaces DATA.HD4).

Verify: `./build.sh ps2-pal` then `verify.py -c` (SCES_511.90: OK); Linux gate; `kazarov shell python3 scripts/port/records.py --build port/build/pc --require-clang`; `ctest -R 'Records|Truncations|Uptr'`.

## W4 `present` (s/present, kz/present)

Done:
- Bone-palette interpolation (Kazarov `port/gfx/displaylist.cpp` `Interpolated`, `BlendProg0Bones`, `MoveProg0`; `Replay` takes a per-record prog0 override). The prog0 block's `model_world`, `clip_or_view`, object-space `eye` and point lights follow the interpolated model and camera. Chronicle records no prog0 block. Tests: `GfxDisplayList.BonesBlendPerBoneAtHalf`, `GfxMeshDc2.SkinnedPaletteInterpolatesInDisplayFrame` (in `port/tests/gfx_displaylist_test.cpp`, suite GfxMeshDc2 for the 30 s budget).
- HUD anchors: `port/src/dng_status.cpp` (`DrawStatusBord`, top-left), `port/src/automap.cpp` (mini map, top-right). `gfx::UiAnchor::Side(x, y, true)` moves the piece's scissor with it (`GfxMeshDc2.AnchoredScissorMovesWithTheDraw`). DC2 has no town clock HUD.
- Letterbox (`video.aspect = 4:3`) clips every main-target draw and clear to the frame (the bars held 3D before); frame flanks sample clamped (the 1-px seam at the frame's leading edges is gone). Tests `GfxMeshDc2.LetterboxClipsMeshesAndClearsToTheFrame`, `FillKeepsClearsAcrossTheWindow`.
- `mgSetPkMoveImage` of the frame (5-argument overloads: `sceneevent.cpp` heat haze, `map.cpp` screen, menu grabs) goes to a frame target as a 1:1 logical copy; draws into a grabbed TBP go to the grab (`FrameGrabAt`). The 6-argument stretch (`screeneffect.cpp`) is unchanged. Test `MglibPresent.MoveImageOfTheFrameUsesAFrameTarget`.
- `--screenshot-display N` (display frames of the screenshot's frame) and `--screenshot-fps`.

Measured (Windows, RTX 4090, title frame 2500): windowed 2560x1440 immediate, max_fps 0: 215-404 FPS at 420-590 draws, TICK 50.0/50; interpolation off: 50/s at the title, 25/s in the movie. Headless `--display-per-tick 4` byte-identical at 1024x768 and 1280x720; letterbox bars black, centre vs a 960x720 render: 23 px differ by 1. Logical-frame PSNR (4:3 frame, box-resampled to 512x480) against 1024x768: 1280x720 38.5 dB, 3840x2160 40.9, 800x1200 35.0, 800x600 38.4, 960x720 39.7 (the same-aspect floor: 40 dB is not reachable from resolution alone). Title `--screenshot-display 4`: consecutive display frames differ (~16% of pixels, camera).

Left:
- In-game proof of interpolated skinned poses, and the HUD probe and matrix in the first town and a dungeon: the attract loop is title <-> opening movie (2D, no meshes; the W3 note calling frame 4500 "skinned characters" is wrong), and nothing past START is reachable until the sweep fixes the hang. Re-run with the sweeps' input scripts: `--window WxH --frames N --screenshot X.png --screenshot-display 4`, then compare consecutive display frames.
- FPS counter in the game's font: not done on purpose (font texture per mode block, `FontTex[0]` dangles across modes; Chronicle made the same choice). It uses the overlay's Compendium font.
- Audit #14 (render targets keyed by handle instead of TBP0): not started; `FrameGrabAt` adds one more TBP-keyed lookup.
- Letterbox centre is within 1 of a 960x720 render on 23 pixels, not byte-identical (3D rasterised in a wider viewport).
- The sweep's report of a 3840x2160 windowed frame offset right with a bar on the left did not reproduce at the language select (3840x2160 client at 0,0 on a 4K panel at 150%, presented frame and canonical image both centred; also 2560x1440 and 1280x720). Need its command line and screen.
- The matrix scripts used: `<scratchpad>/present/matrix.sh` and `imgcmp.py` (not in the repo; scripts/port is the options stream's).

## W4 sweep

One stream (`s/sweep`, worktree `ChronicleTwo-sweep`), town path first. Runs are Windows headless (`darkchronicle.exe --headless --data data --save port/build/windows-x64/save --input <script> --frames N --screenshot out.png`) and one windowed run with a fresh default config.

Landed:
- `ac8f518` (pushed): New Game hung, or exited 1 when windowed. `CMenuCostumeSel::LoadMenuData` (`ps2/src/menuchr.cpp:6668`) spins on `do {} while (ReadBGSync() == 0);` until a queued read reports pending, but the port's `LoadFileBG` finished every read on the spot. Now the slot stays pending: `ReadBG` issues it on one call and completes it on the next, and `ReadBGSync` calls `ReadBG` first, as retail does (`port/src/dataread.cpp`). Tests: `DataRead.BackgroundReadCompletesOnTheSecondSync`, `DataRead.CostumeLoaderWaitSeesPendingRead`.
- `20e9004`: confirming the costume starts the opening (`InitDungeonMain`, floor 20, event 1010), which aborted with an arena overflow, 4543 of 4500 quadwords, under a garbage name, in `SetActionScript` (`ps2/src/actscript.cpp:2613`) <- `LoadActionFile` <- `SetupMainUnit` (`charasetup.cpp:287`). Two causes:
  - `SetupMainUnit` indexes `stacks[6]` of the 6-element `BaseCharacter`. On the PS2 that slot is `BuffTempData` by link adjacency. Fix: `port/src/dng_main.cpp` overrides the variable as `PORT_OVERRIDE mgCMemory BaseCharacter[CHARA_MEM_STACK_NUM]`; port_override leaves the ps2 copy an extern. Merging the array in `ps2/src` instead is not byte-identical: `__sinit_dng_main_cpp` and `.bss` change.
  - `mem_table` counted `RS_STACKDATA`/`RS_CALLDATA` at PS2 pointer widths. `CHARA_SCRIPT_RUNTIME_DELTA` in `ps2/src/charasetup.cpp` is 0 on the PS2 and 448 quadwords on PC, added to the script slots (row 0 slots 5 and 6, row 1 slot 4).
  - `ps2-pal` SCES_511.90 OK, every section including `.bss`. Test: `DngMain.BaseCharacterHoldsTheTempStack`.
- `c872f4e`: `CRedMarkModel::Draw` and `CLockOnModel::Draw` were stubs only because of the `void` -> `int` header fixup. Now they are the retail bodies returning 0 (`port/src/dng_event.cpp`, `port/src/dng_hud.cpp`). Tests: `DngDraw.RedMarkDrawsOnlyWhenRequested`, `DngDraw.LockOnWithoutPlayerDrawsNothing`.
- Gates run:
  - `ac8f518`: Linux build plus `DataRead|Smoke` 22/22; Windows build plus `DataRead|Smoke` 22/22; the windowed run reaches "Confirm Max's costume?".
  - `20e9004`, before the rebase: Linux full ctest 780/780 (data cases skip in a worktree).
  - `c872f4e`: Linux build plus `DngDraw|DngMain|DataRead|Smoke` 25/25; Windows `DngDraw|DngMain|Smoke`.
  - After the rebase onto `79e5d84`: no gate run. The full Windows test gate has not been run on the last two commits.

Furthest point: "Confirm Max's costume?" (`port/build/sweep/t6.png` headless, `port/build/sweep/hf-w.png` windowed at 3840x2160, both in the sweep worktree; the windowed frame sits right of centre with a black bar on the left, already reported to `present`). Selecting YES loads the opening event.

Next blocker: in the opening event, after `---- EVENT LOAD FILE [e50_soldier.chr] ----`, the process dies of a stack overflow (0xc00000fd). `mgCFrame::SearchFrame(name="null39")` (`port/src/mg_frame.cpp:1156`) recurses forever around a cycle of nine frames (0x...97a10 -> 978e0 -> 977b0 -> 97680 -> 97550 -> 945d0 -> 944a0 -> 94370 -> 94240 -> 97a10). The frame tree has a loop: a child or sibling link was written twice, probably while the event's `.chr` frames were attached (`e50_monica.chr`/`e50_soldier.chr`; the search compares against "blade_col_l"). Reproduce: `tools/input/sweep-newgame.input`, then `4450 down/4470 down/4490 down/4520 cross` (costume OK) and `4920 left/4950 cross` (YES); run `--frames 7000` under lldb with `-k "bt 40"`. Look at who links frames on PC (`mgCFrame::SetParent`/`AddChild` in `port/src/mg_frame.cpp` or retail `mg_frame.cpp`, the CHR load in the event code) for a pointer field read or written at a 32-bit stride.

Input scripts: `tools/input/sweep-title.input` (language select -> title -> START, title menu at 2800) and `tools/input/sweep-newgame.input` (+ New Game, costume screen at 4000). The costume and opening lines are above.

Not done:
- Gyorace (`sgEffectDrawGyoRace` + `sgLoopGyoRace`, still `PS2_UNIMPLEMENTED` in `port/src/gyorace.cpp`).
- `event_func` `_SWE_SET_COLOR`/`_SWE_SET_TEXTURE`/`_SWE_START_EFFECT` strides.
- `MenuChapterInit`, `CMenuItemUse::UseItem`.

Debug mode (asked by the user: on by default, documented). Researched, not implemented:
- Boot (`MainLoop`, retail `ps2/src/mainloop.cpp:508-515`, PC copy `port/src/mainloop.cpp:97-104`): `MasterDebugCode = MASTER_DEBUG_CODE` (0x5D44, `mainloop.hpp:203`) when pad 2 holds L1+L2+R1+R2. Then `DebugFlag = false` and `GamePad__2.DebugKeyLock(1)` locks pad 2. The only consumer is `title.cpp:2377`: pad 2 is unlocked only if `MasterDebugCode` matches and `MasterDebugModeOn` is set. That flag comes from `CMemoryCardManager::CheckDebugCode()` (`memcard.cpp:460`, a save with `debug_code > 0`, `title.cpp:2281`), so retail also needed a developer save.
- `DebugFlag` (`mainloop.cpp:2112`) is the real switch, toggled by pad 2 Circle (`mainloop.cpp:677`).
- Pad 2 also drives the following, none of them gated on `DebugFlag`: pause on Select, then Right/pad 1 Circle steps a frame and pad 2 Start stores a frame image (`mainloop.cpp:724-741`, in edit, dungeon and title); L1 toggles the performance meter (`mainloop.cpp:671`); in town, Right/Left scrub the time and R2 toggles rain (`editloop.cpp:1080-1092`, `1879`).
- With `DebugFlag` on, pad 1 controls:
  - Title: Start+Select leaves to the `LOOP_MENU` debug menu (`title.cpp:848`). Its top rows are map / dungeon / title / chrview / texview / mapview / sound / movie views, language, items, save edit, `dbg/game%d.cfg`, save convert. Its event select holds chapter starts, sub-games, Palm Brinks, event viewer, boss battle, future map (`mainloop3.cpp:115`), diorama, HDD and extra.
  - Dungeon: R3 opens the dungeon debug menu (`dng_debug.cpp`: run event, enemy loader, debug camera, skip floor and so on), and Start+Select exits the floor.
  - Town/georama: R3 opens the edit debug menu (`editdebug.cpp`: camera, event run and reload, georama overlay, sub-game start, map jump). R2 makes materials free; L3/Square moves at 3x.
  - Menus: R3 toggles `menu_debug_flag`.
  - Events: Start skips a movie (`event_func.cpp:4776`).
- PC gaps:
  - The keyboard feeds only pad 1 (Kazarov `input.cpp` `Compose`), so pad 2 needs a second controller.
  - The `developer_menu`/`debug_menu` host actions are bound (paddle2/paddle1) but nothing reads them.
  - Capture/Play on the debug top screen hit the `CGamePad` capture stubs; the GyoRace sub-game hits `sgLoopGyoRace`.
- Planned implementation, modelled on Chronicle's `game.debug_mode`:
  - A `game.debug_mode` key, default true, in `port/src/platform/config.*` (parse, serialize).
  - In the `MainLoop` override, set `MasterDebugCode` when it is on.
  - Treat the developer save as present: override `CheckDebugCode`, or set `MasterDebugModeOn`.
  - A pad-1 route to pad 2's toggle (for example L1+L2+R1+R2 held + Circle, as Chronicle's L1+R1+L2+R2+R3).
  - `docs/DEBUG.md`, and a test that `debug_mode` sets `MasterDebugCode` and false leaves it 0.

## Stream `text2d` (W4)

- Done: nearest-filtered sprites sample the GS's texels (font bleed, background tile seams), and the `DrawVersatileWin` backings cover the frame opening (a deliberate deviation from retail; see docs/PC.md "Sprites and window backings").
- Left: the same top-left sampling rule is not applied to textured triangles or to linear-filtered sprites (GS bilinear at 1:1 integer UVs blends the texel before the edge; the shader's footprint already models that, unverified at upscales).
- Verify: `ctest -R "MgDrawPrim|MesWin"`; costume screen via the sweep New Game script plus `3200 circle`, `3300/3330/3360 down`, `3400 cross` (6-frame presses), `darkchronicle --headless --window 1280x960 --frames 3500 --screenshot out.png`: no dots under glyphs, no black lines at window x 117/757, the dialog's upper section evenly dark.

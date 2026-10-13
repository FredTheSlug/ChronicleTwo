#pragma once

#include <cstdint>

class CCameraControl;
class CScene;
class CPadControl;
struct CCPoly;
struct mgVu0FBOX;

// Returns -1 when collection cannot prove complete coverage within capacity.
int MouseCameraCollectPolys(CScene &scene, mgVu0FBOX &box, CCPoly *out, int capacity);

// Only the active, unlocked walking camera may take a mouse sample. The caller
// supplies whether the town loop is running, so other camera modes keep ownership.
bool TownMouseCameraOwns(CCameraControl *camera, CScene *scene, CPadControl *pad,
                        bool walking, bool subgame, bool locked);

// Apply the safe prefix of a direct yaw to both current and pending poses.
// Polygons must cover the entire orbit and follow corridor, not just the old eye.
// No refused motion is carried into another input read.
float MouseCameraYaw(CCameraControl &camera, float yaw, CCPoly *polys, int count);

// Tilt the follow view by changing eye height, within the active retail limits.
// Returns the accepted height delta; the look-at point remains on the character.
float MouseCameraPitch(CCameraControl &camera, float pitch, CCPoly *polys, int count);

#include "common.h"
#include "camera_zoom.hpp"
#include "camera_mouse.hpp"
#include "scenesnd.hpp"

#include <algorithm>
#include <cmath>

namespace {
float Distance(const float *eye, const float *target) {
    return std::hypot(eye[0] - target[0], eye[2] - target[2]);
}

void Copy(float *to, const float *from) {
    for (int i = 0; i < 3; ++i) to[i] = from[i];
}
}

void CameraZoom::Read(CCameraControl &camera, float wheel, bool reset, bool enabled) {
    if (!enabled || reset) {
        if (active) {
            requested = camera.GetActiveParam()->min_dist;
            returning = true;
        }
        return;
    }
    if (!std::isfinite(wheel) || wheel == 0) return;
    if (!active) requested = Distance(camera.next_pos, camera.next_ref);
    if (!std::isfinite(requested)) return;
    // Bound the multiplication as well as the result; enormous finite wheel
    // samples must behave like reaching a limit, never introduce infinity.
    requested = std::clamp(requested - std::clamp(wheel, -1000.0f, 1000.0f) * wheel_step,
                           min_distance, max_distance);
    active = true;
    returning = false;
}

void CameraZoom::Move(CCameraControl &camera, CScene &scene,
                      CCameraControl::Control &control, float *rotation) {
    CCameraControl candidate = camera;
    CameraCtrlParam &param = *candidate.GetActiveParam();
    const CameraCtrlParam retail = param;
    // Never set equal limits: retail interpolates height by max_dist-min_dist.
    // Retain its height curve at the requested distance without extrapolating
    // the near-player lift through the floor at long zoom distances.
    const float span = std::max(1.0f, retail.max_dist - retail.min_dist);
    const float ratio = std::clamp((requested - retail.min_dist) / span, 0.0f, 1.0f);
    param.min_dist = requested;
    param.max_dist = requested + 0.01f;
    param.near_height = param.far_height = retail.near_height + ratio * (retail.far_height - retail.near_height);

    // First generate the normal follow, controller and reset destination. Its
    // geometry is collected below, including the new distance and follow point.
    param.no_check = 1;
    candidate.MoveCamera(&control, rotation, nullptr, 0);
    const float height = param.height;
    param = retail;
    param.height = height;
    param.no_check = 0;

    mgVu0FBOX bounds{};
    for (int i = 0; i < 3; ++i) {
        bounds.min[i] = std::min({camera.pos[i], camera.ref[i], candidate.next_pos[i], candidate.next_ref[i]}) - 8;
        bounds.max[i] = std::max({camera.pos[i], camera.ref[i], candidate.next_pos[i], candidate.next_ref[i]}) + 8;
    }
    // Include the full vertical query used by CheckGround, not only the eye.
    bounds.min[1] = std::min(bounds.min[1], candidate.next_ref[1] + param.min_height - param.ground_space - 8);
    bounds.max[1] = std::max(bounds.max[1], candidate.next_ref[1] + param.max_height + 28);
    bounds.min[3] = bounds.max[3] = 1;
    CCPoly polys[512];
    const int count = MouseCameraCollectPolys(scene, bounds, polys, 512);
    if (count >= 0) {
        candidate.CheckGround(polys, count);
        candidate.CheckCollision(polys, count);
    }
    // Only next poses are committed. Retail's Step supplies smooth motion, and
    // the proof covers independent eye/target easing and thin walls in between.
    MouseCameraClampFollow(camera, candidate, polys, count);
    camera.GetActiveParam()->height = height;
    camera.rot_back = candidate.rot_back;
    camera.rot_back_angle = candidate.rot_back_angle;

    if (returning && std::fabs(Distance(camera.pos, camera.ref) - requested) < 0.1f &&
        std::fabs(Distance(camera.next_pos, camera.next_ref) - requested) < 0.1f) {
        active = returning = false;
    }
}
